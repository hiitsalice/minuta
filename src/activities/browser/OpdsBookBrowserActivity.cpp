#include "OpdsBookBrowserActivity.h"

#include <Arduino.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <OpdsStream.h>
#include <WiFi.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
constexpr uint32_t MIN_FREE_HEAP = 60000;  // stop loading pages if memory gets tight
constexpr size_t MAX_ENTRIES = 300;        // stop growing here to protect memory

// Feeds such as ManyBooks wrap titles in markup with line breaks and spaces. Trim the edges,
// collapse inner whitespace, and drop entries without a title (they would show as blank rows).
void tidyEntries(std::vector<OpdsEntry>& list) {
  const auto tidy = [](std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (const char c : text) {
      if (isspace(static_cast<unsigned char>(c))) {
        pendingSpace = !out.empty();
      } else {
        if (pendingSpace) out += ' ';
        pendingSpace = false;
        out += c;
      }
    }
    text.swap(out);
  };
  for (auto& entry : list) {
    tidy(entry.title);
    tidy(entry.author);
  }
  list.erase(std::remove_if(list.begin(), list.end(), [](const OpdsEntry& e) { return e.title.empty(); }), list.end());
}
constexpr int DOWNLOAD_PROGRESS_STEP_PERCENT = 5;
constexpr unsigned long DOWNLOAD_PROGRESS_MIN_UPDATE_MS = 5000;
constexpr unsigned long SEARCH_HOLD_MS = 700;  // hold Select/Load this long to open Search
bool searchHoldArmed = false;                  // a Confirm press that began on the list may become a Search hold

void drawEmptyButtonHints(GfxRenderer& renderer, const MappedInputManager& mappedInput) {
  // Match WifiSelectionActivity::renderConnecting() exactly.
  const auto labels = mappedInput.mapLabels("", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

}  // namespace

OpdsBookBrowserActivity::OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 OpdsServer server, const bool returnToParentOnBack)
    : Activity("OpdsBookBrowser", renderer, mappedInput),
      UiAppHost(renderer),
      buttonNavigator(),
      server(std::move(server)),
      returnToParentOnBack(returnToParentOnBack) {}

void OpdsBookBrowserActivity::onEnter() {
  Activity::onEnter();

  state = BrowserState::CHECK_WIFI;
  entries.clear();
  navigationHistory.clear();
  pageTrail.clear();
  trailHistory.clear();
  hasReturnPage = false;
  searchTemplate = "";
  loadingMore = false;
  loadingEntries.clear();
  loadingCursor.clear();
  currentPath = "";
  selectorIndex = 0;
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);

  listNav.reset();
  resetUi();
  app.on(ACTION_ROW, &OpdsBookBrowserActivity::onRowEvent, this);
  app.setScreen(&OpdsBookBrowserActivity::rootScreen, this);

  requestUpdate();
  checkAndConnectWifi();
}

void OpdsBookBrowserActivity::onExit() {
  Activity::onExit();
  entries.clear();
  navigationHistory.clear();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_MODE_NULL);
  }
}

void OpdsBookBrowserActivity::activateSelected() {
  if (entries.empty() || selectorIndex < 0 || selectorIndex >= static_cast<int>(entries.size())) return;
  const auto& entry = entries[selectorIndex];
  entry.type == OpdsEntryType::BOOK ? downloadBook(entry) : navigateToEntry(entry);
}

void OpdsBookBrowserActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<OpdsBookBrowserActivity*>(user);
  if (self->state != BrowserState::BROWSING) return;
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->entries.size())) return;
  self->selectorIndex = event.value;
  // The tapped row leaves the screen either way (new feed or download view);
  // a lingering tap flash would gray an unrelated row on the next list.
  self->app.clearTapFlash();
  self->activateSelected();
}

void OpdsBookBrowserActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION || state == BrowserState::SEARCH_INPUT) {
    return;
  }

  if (state == BrowserState::ERROR) {
    // Error screens have no Retry: only Back works.
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      leaveError();
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI) {
    return;  // all buttons are disabled while checking Wi-Fi
  }

  if (state == BrowserState::LOADING) {
    // All buttons are disabled while loading a feed. Pages load one per pass, so the screen keeps updating.
    if (loadingMore) stepLoading();
    return;
  }

  if (state == BrowserState::DOWNLOADING) return;

  if (state == BrowserState::NO_SEARCH) {
    // Info screen: only Back works, and it returns to the list.
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = BrowserState::BROWSING;
      requestUpdate();
    }
    return;
  }

  if (state == BrowserState::BROWSING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // Only a press that started on this list can become a Search hold.
      searchHoldArmed = true;
    }
    if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, SEARCH_HOLD_MS)) {
      // Holding Select/Load opens Search, or says the server has none.
      if (searchHoldArmed && !entries.empty()) {
        if (!searchTemplate.empty()) {
          launchSearch();
        } else {
          state = BrowserState::NO_SEARCH;
          requestUpdate();
        }
      }
      searchHoldArmed = false;
      mappedInput.suppressRelease(MappedInputManager::Button::Confirm);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      searchHoldArmed = false;
      // One button for both: Select opens a category or page row, Load downloads a book.
      activateSelected();
      return;
    }

    if (!entries.empty()) {
      const auto moveSelection = [this](const int index) {
        selectorIndex = index;
        listNav.selected = index;
        listNav.follow(static_cast<int>(entries.size()));
        // every page is loaded when the feed opens
        requestUpdate();
      };
      // Browsing uses the side buttons exclusively for list navigation. The
      // front Left/Right buttons are reserved for Load and Search.
      buttonNavigator.onRelease(
          {MappedInputManager::Button::Down, MappedInputManager::Button::Right},
          [this, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectorIndex, entries.size())); });
      buttonNavigator.onRelease(
          {MappedInputManager::Button::Up, MappedInputManager::Button::Left},
          [this, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectorIndex, entries.size())); });
      buttonNavigator.onContinuous(
          {MappedInputManager::Button::Down, MappedInputManager::Button::Right},
          [this, &moveSelection] { moveSelection(std::min(selectorIndex + 6, static_cast<int>(entries.size()) - 1)); });
      buttonNavigator.onContinuous({MappedInputManager::Button::Up, MappedInputManager::Button::Left},
                                   [this, &moveSelection] { moveSelection(std::max(selectorIndex - 6, 0)); });
    }
  }
}

void OpdsBookBrowserActivity::rootScreen(UiScreen& screen, void* user) {
  auto* self = static_cast<OpdsBookBrowserActivity*>(user);
  switch (self->state) {
    case BrowserState::BROWSING:
      self->buildBrowsingScreen(screen);
      break;
    case BrowserState::DOWNLOADING:
      self->buildDownloadScreen(screen);
      break;
    default:
      self->buildStatusScreen(screen);
      break;
  }
}

// Match FontDownloadActivity's family-list typography and content geometry.
void OpdsBookBrowserActivity::buildBrowsingScreen(UiScreen& screen) {
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  const auto& metrics = UITheme::getInstance().getMetrics();
  // Same content margins and row geometry as the Settings list: left aligned, one line per row.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 3), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, 400, tr(STR_NO_ENTRIES));
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.rowGap = 0;
  props.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  props.labelText.lineGap = 6;
  listNav.selected = selectorIndex;
  listNav.syncToProps(screen.body(), props.rowHeight, screen.theme().listRowGap, static_cast<int>(entries.size()),
                      props);
  screen.list(props);
}

void OpdsBookBrowserActivity::buildDownloadScreen(UiScreen& screen) {
  // The progress screen uses fixed-pixel rendering like OTA; keep this FUI
  // screen empty so no stale touch action remains available.
  (void)screen;
}

void OpdsBookBrowserActivity::buildStatusScreen(UiScreen& screen) {
  // Status and error messages use fixed 10pt coordinates shared with OTA.
  (void)screen;
}

void OpdsBookBrowserActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, pageWidth, metrics.headerHeight}, tr(STR_OPDS_BROWSER));

  // Rebuild/publish the app interaction table for every state. Only BROWSING
  // has interactive rows; status, download and error pages publish none.
  renderUi();

  switch (state) {
    case BrowserState::BROWSING: {
      // Second button: Select on a category/page row, Load on a book row. Holding it opens Search.
      const bool onBook = !entries.empty() && selectorIndex >= 0 && selectorIndex < static_cast<int>(entries.size()) &&
                          entries[selectorIndex].type == OpdsEntryType::BOOK;
      const char* actionLabel = entries.empty() ? "" : (onBook ? "Load" : tr(STR_SELECT));
      const bool hasEntries = !entries.empty();
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), actionLabel, hasEntries ? tr(STR_DIR_UP) : "",
                                                hasEntries ? tr(STR_DIR_DOWN) : "");
      const bool listFillsArea = listNav.visibleRows > 0 && entries.size() >= static_cast<size_t>(listNav.visibleRows);
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, -1, listFillsArea);
      break;
    }

    case BrowserState::DOWNLOADING: {
      // Same progress bar as the OTA update screen: percentage at Y=367, bar top at Y=394, 12px high.
      const size_t clampedProgress = std::min(downloadProgress, downloadTotal);
      const int percent =
          downloadTotal > 0 ? static_cast<int>((static_cast<uint64_t>(clampedProgress) * 100) / downloadTotal) : 0;
      GUI.drawProgressBar(renderer,
                          Rect{metrics.contentSidePadding, 394, pageWidth - metrics.contentSidePadding * 2, 12},
                          percent, 100, UI_10_FONT_ID, 367);
      UITheme::drawCenteredText(renderer, UITheme::getInstance().getScreenSafeArea(renderer, true, false),
                                UI_10_FONT_ID, 418, statusMessage.c_str(), true, EpdFontFamily::Style::ITALIC);
      drawEmptyButtonHints(renderer, mappedInput);
      break;
    }

    case BrowserState::ERROR: {
      renderer.drawCenteredText(UI_10_FONT_ID, 386, tr(STR_ERROR_MSG), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, 414, errorMessage.c_str());
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case BrowserState::NO_SEARCH: {
      renderer.drawCenteredText(UI_10_FONT_ID, 400, "Search is not available");
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    default:
      // CHECK_WIFI and LOADING share the same layout and keep only the
      // empty hint strip, with no assigned hint actions.
      renderer.drawCenteredText(UI_10_FONT_ID, 400, statusMessage.c_str());
      drawEmptyButtonHints(renderer, mappedInput);
      break;
  }

  renderer.displayBuffer();
}

// Fetches one feed page. Returns 0 on success, 1 if the request failed, 2 if the feed could not be read.
// Fetches one feed page. Returns 0 on success, 1 if the request failed, 2 if the feed could not be read.
int OpdsBookBrowserActivity::fetchPage(const std::string& url, std::vector<OpdsEntry>& out, std::string& next,
                                       std::string* search, bool* truncated) {
  OpdsParser parser;
  {
    OpdsParserStream stream{parser};
    if (!HttpDownloader::fetchUrl(url, stream, server.username, server.password)) return 1;
  }
  if (!parser) return 2;
  if (search) *search = parser.getSearchTemplate();
  if (truncated) *truncated = parser.truncated();
  next = parser.getNextPageUrl();
  out = std::move(parser).getEntries();
  return 0;
}

void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  if (server.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_SERVER_URL);
    requestUpdate();
    return;
  }

  const std::string url = UrlUtils::buildUrl(server.url, path);
  LOG_DBG("OPDS", "Fetching: %s", url.c_str());

  std::vector<OpdsEntry> loaded;
  std::string next;
  std::string search;
  bool truncated = false;
  const int result = fetchPage(url, loaded, next, &search, &truncated);
  if (result == 1) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }
  if (result == 2) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }
  if (truncated) LOG_INF("OPDS", "Feed truncated to fit memory");

  searchTemplate = search;
  nextPageUrl.clear();  // no Next/Previous rows: the whole section is one list
  prevPageUrl.clear();

  // The remaining pages load one per loop pass (see stepLoading), so the loading screen keeps updating.
  loadingEntries = std::move(loaded);
  loadingCursor = next.empty() ? std::string() : UrlUtils::buildUrl(url, next);
  loadingPages = 0;
  lastLoadingUpdateMs = 0;
  loadingMore = !loadingCursor.empty();
  if (!loadingMore) finishLoading();
}

// Loads the next page of the section. A page that cannot be loaded just ends the list, quietly.
void OpdsBookBrowserActivity::stepLoading() {
  while (ESP.getFreeHeap() <= MIN_FREE_HEAP + 30000 && dropOldestSavedList()) {
  }
  if (loadingPages >= 200 || loadingCursor.empty() || loadingEntries.size() >= MAX_ENTRIES ||
      ESP.getFreeHeap() <= MIN_FREE_HEAP) {
    finishLoading();
    return;
  }

  std::vector<OpdsEntry> more;
  std::string nextHref;
  const std::string pageUrl = loadingCursor;
  if (fetchPage(pageUrl, more, nextHref, nullptr, nullptr) != 0 || more.empty()) {
    finishLoading();
    return;
  }
  loadingEntries.insert(loadingEntries.end(), std::make_move_iterator(more.begin()),
                        std::make_move_iterator(more.end()));
  loadingCursor = nextHref.empty() ? std::string() : UrlUtils::buildUrl(pageUrl, nextHref);
  if (loadingCursor == pageUrl) loadingCursor.clear();
  ++loadingPages;

  if (loadingCursor.empty()) {
    finishLoading();
    return;
  }
  // Show progress now and then: every update refreshes the e-ink panel.
  if (millis() - lastLoadingUpdateMs >= 1000) {
    lastLoadingUpdateMs = millis();
    {
      RenderLock lock;
      statusMessage = "Loading feed... " + std::to_string(loadingEntries.size()) + " entries";
    }
    requestUpdate(true);
  }
}

void OpdsBookBrowserActivity::finishLoading() {
  loadingMore = false;
  tidyEntries(loadingEntries);

  // Reset the selection before the swap: the render task reads
  // entries[selectorIndex] under only an empty() guard, and the new feed can
  // be shorter than the old selection.
  selectorIndex = 0;
  listNav.reset();
  entries = std::move(loadingEntries);
  loadingEntries.clear();
  loadingCursor.clear();

  // An empty feed stays on the browsing screen and shows "No entries found" with only Back.
  state = BrowserState::BROWSING;
  hasReturnPage = false;
  rebuildRowItems();
  requestUpdate();
}

void OpdsBookBrowserActivity::rebuildRowItems() {
  rowItems.clear();
  rowLabels.clear();
  rowItems.reserve(entries.size());
  rowLabels.reserve(entries.size());

  const auto& metrics = UITheme::getInstance().getMetrics();
  // Text width a row can use (a little under, so nothing ever wraps).
  const int maxWidth = renderer.getScreenWidth() - 2 * (metrics.listInset + metrics.listSidePadding) - 16;

  const auto widthOf = [&](const std::string& text) {
    return renderer.getTextAdvanceX(UI_10_FONT_ID, text.c_str(), EpdFontFamily::REGULAR);
  };
  const auto dropLastChar = [](std::string& text) {
    if (text.empty()) return;
    size_t i = text.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) --i;
    text.resize(i);
  };
  // One line only: drop whole words from the end until it fits, then add "...".
  const auto fit = [&](const std::string& text) {
    if (widthOf(text) <= maxWidth) return text;
    std::string out = text;
    while (true) {
      const size_t cut = out.find_last_of(' ');
      if (cut == std::string::npos) break;
      out.resize(cut);
      if (widthOf(out + "...") <= maxWidth) return out + "...";
    }
    while (out.size() > 1 && widthOf(out + "...") > maxWidth) dropLastChar(out);
    return out + "...";
  };

  // Ask the UI font which letters it can draw.
  const auto& fontMap = renderer.getFontMap();
  const auto fontIt = fontMap.find(UI_10_FONT_ID);
  const bool hasSdFonts = !renderer.getSdCardFonts().empty();
  const auto canDraw = [&](uint32_t cp) {
    if (cp <= 0x20 || cp == 0xA0) return true;
    // Chinese, Japanese and Korean letters are drawn by an installed SD font, when there is one.
    if ((cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFF00 && cp <= 0xFFEF)) {
      return hasSdFonts;
    }
    return fontIt == fontMap.end() || fontIt->second.hasCodepoint(cp, EpdFontFamily::REGULAR);
  };
  // A title the font mostly cannot draw would be a blank row: show a placeholder.
  // A few undrawable letters show as "?", and marks that cannot stand alone are dropped.
  const auto drawable = [&](const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t total = 0;
    size_t missing = 0;
    for (size_t i = 0; i < text.size();) {
      const unsigned char c = static_cast<unsigned char>(text[i]);
      uint32_t cp = c;
      size_t len = 1;
      if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
        cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
        len = 2;
      } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
        cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
        len = 3;
      } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
        cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 12) |
             ((static_cast<unsigned char>(text[i + 2]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(text[i + 3]) & 0x3Fu);
        len = 4;
      }
      const std::string piece = text.substr(i, len);
      i += len;
      if (cp <= 0x20) {
        out += piece;
        continue;
      }
      if (canDraw(cp)) {
        ++total;
        out += piece;
        continue;
      }
      if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x200B && cp <= 0x200F) || cp == 0xFEFF) continue;
      ++total;
      ++missing;
      out += '?';
    }
    if (text.empty()) return text;
    if (total == 0 || missing * 2 > total || widthOf(out) <= 0) return std::string("(title not shown)");
    return out;
  };

  for (const auto& entry : entries) {
    rowLabels.push_back(fit(drawable(entry.title)));
  }
  for (size_t i = 0; i < entries.size(); ++i) {
    fui::ListItem item;
    item.label = rowLabels[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void OpdsBookBrowserActivity::releaseEntries() {
  // The app's interaction table holds row indices (and hit rects) for the old
  // entries; stop routing touches against it until the next render.
  closeRouting();
  std::vector<OpdsEntry>().swap(entries);
  rebuildRowItems();
}

void OpdsBookBrowserActivity::navigateToEntry(const OpdsEntry& entry) {
  // Next/Previous Page rows replace the current page; only categories add a Back step.
  const bool isPageRow = !entry.href.empty() && (entry.href == nextPageUrl || entry.href == prevPageUrl);
  if (!isPageRow) {
    navigationHistory.push_back(currentPath);
    trailHistory.push_back(pageTrail);
    pageTrail.clear();
    hasReturnPage = false;
  } else {
    // A failed page turn returns to the page we are on now.
    hasReturnPage = true;
    returnPath = currentPath;
    returnTrail = pageTrail;
    if (entry.href == nextPageUrl) {
      pageTrail.push_back(UrlUtils::buildUrl(server.url, currentPath));
    } else if (!pageTrail.empty()) {
      pageTrail.pop_back();
    }
  }
  // Resolve to a full URL so sub-sub-navigation retains parent path context
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  currentPath = UrlUtils::buildUrl(feedUrl, entry.href);
  saveListForBack();

  state = BrowserState::LOADING;
  statusMessage = "Loading feed...";
  releaseEntries();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::leaveError() {
  if (!hasReturnPage) {
    navigateBack();
    return;
  }
  // A failed page turn or download: go back to the page we were on.
  hasReturnPage = false;
  currentPath = returnPath;
  pageTrail = returnTrail;
  state = BrowserState::LOADING;
  statusMessage = "Loading feed...";
  releaseEntries();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(currentPath);
}

// Puts the list we are leaving aside (with its selection and scroll position) so Back can restore it
// without loading it again. Called right after navigationHistory gets the path of that list.
void OpdsBookBrowserActivity::saveListForBack() {
  SavedList saved;
  saved.path = navigationHistory.empty() ? std::string() : navigationHistory.back();
  saved.searchTemplate = searchTemplate;
  saved.selectorIndex = selectorIndex;
  saved.nav = listNav;
  {
    RenderLock lock;
    saved.entries = std::move(entries);
    entries.clear();
  }
  saved.valid = !saved.entries.empty();
  savedLists.push_back(std::move(saved));
}

// Frees memory by dropping the oldest saved list. The first one is Main: it is tiny and it is
// what Back returns to last, so it is kept.
bool OpdsBookBrowserActivity::dropOldestSavedList() {
  for (size_t i = 1; i < savedLists.size(); ++i) {
    auto& l = savedLists[i];
    if (l.valid) {
      std::vector<OpdsEntry>().swap(l.entries);
      l.valid = false;  // this level loads again when we go back to it
      return true;
    }
  }
  return false;
}

void OpdsBookBrowserActivity::navigateBack() {
  if (navigationHistory.empty()) {
    savedLists.clear();
    if (returnToParentOnBack) {
      finish();
    } else {
      onGoHome();
    }
    return;
  }

  currentPath = navigationHistory.back();
  navigationHistory.pop_back();
  hasReturnPage = false;

  SavedList saved;
  bool haveSaved = false;
  if (!savedLists.empty()) {
    saved = std::move(savedLists.back());
    savedLists.pop_back();
    haveSaved = true;
  }
  // Back at the outermost page (Main): free every other saved list.
  const bool backAtMain = navigationHistory.empty();
  if (backAtMain) savedLists.clear();

  if (haveSaved && saved.valid && saved.path == currentPath) {
    // Restore the list we came from instantly, on the row we left.
    {
      RenderLock lock;
      closeRouting();
      entries = std::move(saved.entries);
      searchTemplate = saved.searchTemplate;
      selectorIndex = saved.selectorIndex;
      listNav = saved.nav;
      listNav.selected = selectorIndex;
      nextPageUrl.clear();
      prevPageUrl.clear();
      state = BrowserState::BROWSING;
      rebuildRowItems();
    }
    // Back at Main: a half refresh clears the ghosting left by the deeper screens.
    if (backAtMain) renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
    requestUpdate(true);
    return;
  }

  state = BrowserState::LOADING;
  statusMessage = "Loading feed...";
  releaseEntries();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadProgress = downloadTotal = 0;
  requestUpdate(true);

  // Build full download URL relative to the current feed, not the root server URL
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  std::string downloadUrl = UrlUtils::buildUrl(feedUrl, book.href);
  // opdsDownloadFolder is already a null-terminated char[64]; use it directly —
  // no std::string copy. exists()/mkdir() take const char*.
  const char* folder = SETTINGS.opdsDownloadFolder;  // "" => SD root
  bool haveFolder = folder[0] != '\0';
  if (haveFolder && !Storage.exists(folder) && !Storage.mkdir(folder)) {
    // exists()-guard first: mkdir's return-on-existing is unconfirmed, and every
    // existing caller checks exists() before mkdir. On real failure, fall back
    // to SD root so the download is never lost.
    LOG_ERR("OPDS", "mkdir failed for %s, using SD root", folder);
    haveFolder = false;
  }

  // downloadToFile() needs a std::string, and titles are unbounded (a fixed
  // char[] would truncate). Cold path (a multi-second download follows), so one
  // reserve'd, in-place-appended owning string is the right call.
  std::string filename;
  filename.reserve(96);
  if (haveFolder) filename += folder;
  filename += '/';
  filename += opdsBookFilename(book.author, book.title, static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
  LOG_DBG("OPDS", "Downloading: %s -> %s", downloadUrl.c_str(), filename.c_str());

  int lastRenderedPercent = -1;
  unsigned long lastProgressUpdateMs = 0;
  const auto result = HttpDownloader::downloadToFile(
      downloadUrl, filename,
      [this, &lastRenderedPercent, &lastProgressUpdateMs](const size_t downloaded, const size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        // No front or side buttons are active while the synchronous download runs.
        const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(downloaded) * 100 / total) : 0;
        const unsigned long now = millis();
        if (percent >= 100 || lastRenderedPercent < 0 ||
            percent >= lastRenderedPercent + DOWNLOAD_PROGRESS_STEP_PERCENT ||
            now - lastProgressUpdateMs >= DOWNLOAD_PROGRESS_MIN_UPDATE_MS) {
          lastRenderedPercent = percent;
          lastProgressUpdateMs = now;
          requestUpdate(true);
        }
      },
      nullptr, server.username, server.password);

  if (result == HttpDownloader::OK) {
    clearBookCache(filename);
    state = BrowserState::BROWSING;
  } else if (result == HttpDownloader::ABORTED) {
    // User cancelled; the partial file is already removed. Back to the list.
    LOG_INF("OPDS", "Download cancelled");
    state = BrowserState::BROWSING;
  } else {
    LOG_ERR("OPDS", "Download failed: %d", static_cast<int>(result));
    state = BrowserState::ERROR;
    errorMessage = tr(STR_DOWNLOAD_FAILED);
    // Back from this error returns to the list we were on.
    hasReturnPage = true;
    returnPath = currentPath;
    returnTrail = pageTrail;
  }
  requestUpdate();
}

void OpdsBookBrowserActivity::launchSearch() {
  state = BrowserState::SEARCH_INPUT;
  requestUpdate();

  auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH));
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    state = BrowserState::BROWSING;
    if (!result.isCancelled) {
      performSearch(std::get<KeyboardResult>(result.data).text);
    } else {
      requestUpdate();
    }
  });
}

void OpdsBookBrowserActivity::performSearch(const std::string& query) {
  if (query.empty() || searchTemplate.empty()) {
    state = BrowserState::BROWSING;
    requestUpdate();
    return;
  }

  auto urlEncode = [](const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        out += static_cast<char>(c);
      else {
        char buf[4];
        snprintf(buf, sizeof(buf), "%%%02X", c);
        out += buf;
      }
    }
    return out;
  };

  std::string url = searchTemplate;
  const std::string placeholder = "{searchTerms}";
  const size_t pos = url.find(placeholder);
  if (pos != std::string::npos) url.replace(pos, placeholder.length(), urlEncode(query));

  navigationHistory.push_back(currentPath);
  trailHistory.push_back(pageTrail);
  pageTrail.clear();
  hasReturnPage = false;
  currentPath = url;
  saveListForBack();

  state = BrowserState::LOADING;
  statusMessage = "Loading feed...";
  releaseEntries();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(url);
}

void OpdsBookBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = "Loading feed...";
    requestUpdate(true);
    fetchFeed(currentPath);
    return;
  }
  launchWifiSelection();
}

void OpdsBookBrowserActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OpdsBookBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = BrowserState::LOADING;
    statusMessage = "Loading feed...";
    requestUpdate(true);
    fetchFeed(currentPath);
  } else {
    // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
