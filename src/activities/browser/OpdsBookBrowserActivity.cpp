#include "OpdsBookBrowserActivity.h"

#include <Arduino.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <OpdsStream.h>
#include <WiFi.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
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
constexpr int DOWNLOAD_PROGRESS_STEP_PERCENT = 5;
constexpr unsigned long DOWNLOAD_PROGRESS_MIN_UPDATE_MS = 5000;
constexpr unsigned long SEARCH_HOLD_MS = 700;  // hold Select/Load this long to open Search

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
  searchTemplate = "";
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
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = BrowserState::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchFeed(currentPath);
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI) {
    return;  // all buttons are disabled while checking Wi-Fi
  }

  if (state == BrowserState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  if (state == BrowserState::DOWNLOADING) return;

  if (state == BrowserState::BROWSING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
      return;
    }
    if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, SEARCH_HOLD_MS)) {
      // Holding Select/Load opens Search, only when the server provides a search template.
      if (!searchTemplate.empty()) launchSearch();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // One button for both: Select opens a category or page row, Load downloads a book.
      activateSelected();
      return;
    }

    if (!entries.empty()) {
      const auto moveSelection = [this](const int index) {
        selectorIndex = index;
        listNav.selected = index;
        listNav.follow(static_cast<int>(entries.size()));
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
          {MappedInputManager::Button::Down, MappedInputManager::Button::Right}, [this, &moveSelection] {
            moveSelection(ButtonNavigator::nextPageIndex(selectorIndex, entries.size(), listNav.visibleRows));
          });
      buttonNavigator.onContinuous(
          {MappedInputManager::Button::Up, MappedInputManager::Button::Left}, [this, &moveSelection] {
            moveSelection(ButtonNavigator::previousPageIndex(selectorIndex, entries.size(), listNav.visibleRows));
          });
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
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 2), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  if (entries.empty()) {
    screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.sidePadding = 12;
  props.rowInset = 18;
  props.valueInset = 0;
  props.labelText = screen.theme().bodyText;
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.labelText.bold = true;
  props.labelText.italic = false;
  props.labelText.align = fui::TextAlign::Center;
  props.labelText.maxLines = 1;
  props.labelText.lineGap = 1;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.font = fui::GfxRendererTarget::FONT_BODY;
  props.subtitleText.bold = false;
  props.subtitleText.align = fui::TextAlign::Center;
  props.subtitleText.italic = false;
  props.subtitleText.maxLines = 1;
  props.subtitleText.lineGap = 1;
  props.subtitleGap = 6;

  const int16_t rowHeight = static_cast<int16_t>(metrics.listWithSubtitleRowHeight + 2);
  props.rowHeight = rowHeight;
  props.rowGap = 0;
  listNav.selected = selectorIndex;
  listNav.syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, static_cast<int>(entries.size()), props);
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
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), actionLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
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
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
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

void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  if (server.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_SERVER_URL);
    requestUpdate();
    return;
  }

  std::string url = UrlUtils::buildUrl(server.url, path);
  LOG_DBG("OPDS", "Fetching: %s", url.c_str());
  OpdsParser parser;
  {
    OpdsParserStream stream{parser};
    if (!HttpDownloader::fetchUrl(url, stream, server.username, server.password)) {
      state = BrowserState::ERROR;
      errorMessage = tr(STR_FETCH_FEED_FAILED);
      requestUpdate();
      return;
    }
  }

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  searchTemplate = parser.getSearchTemplate();
  const auto& nextUrl = parser.getNextPageUrl();
  const auto& prevUrl = parser.getPrevPageUrl();
  nextPageUrl = nextUrl;
  prevPageUrl = prevUrl;
  const bool feedTruncated = parser.truncated();
  // Reset the selection before the swap: the render task reads
  // entries[selectorIndex] under only an empty() guard, and the new feed can
  // be shorter than the old selection.
  selectorIndex = 0;
  listNav.reset();
  entries = std::move(parser).getEntries();

  entries.reserve(entries.size() + (prevUrl.empty() ? 0 : 1) + (nextUrl.empty() ? 0 : 1));
  if (!prevUrl.empty()) {
    entries.insert(entries.begin(), OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_PREV_PAGE), "", prevUrl, ""});
  }
  if (!nextUrl.empty()) {
    entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_NEXT_PAGE), "", nextUrl, ""});
  }
  if (feedTruncated) {
    LOG_INF("OPDS", "Feed truncated to fit memory");
  }

  state = entries.empty() ? BrowserState::ERROR : BrowserState::BROWSING;
  if (entries.empty()) errorMessage = tr(STR_NO_ENTRIES);
  rebuildRowItems();
  requestUpdate();
}

// Derives rowItems from entries. Called whenever entries changes
// (fetchFeed()/releaseEntries()) so buildBrowsingScreen() reuses the cached
// rows on every repaint instead of rebuilding them per render.
void OpdsBookBrowserActivity::rebuildRowItems() {
  rowItems.clear();
  rowItems.reserve(entries.size());
  for (const auto& entry : entries) {
    fui::ListItem item;
    item.label = entry.title.c_str();
    if (entry.type == OpdsEntryType::BOOK && !entry.author.empty()) {
      item.subtitle = entry.author.c_str();
    }
    item.actionValue = static_cast<int16_t>(rowItems.size());
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
  if (!isPageRow) navigationHistory.push_back(currentPath);
  // Resolve to a full URL so sub-sub-navigation retains parent path context
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  currentPath = UrlUtils::buildUrl(feedUrl, entry.href);

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  releaseEntries();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::navigateBack() {
  if (navigationHistory.empty()) {
    if (returnToParentOnBack) {
      finish();
    } else {
      onGoHome();
    }
  } else {
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    releaseEntries();
    selectorIndex = 0;
    requestUpdate();
    fetchFeed(currentPath);
  }
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
  currentPath = url;

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  releaseEntries();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(url);
}

void OpdsBookBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
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
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchFeed(currentPath);
  } else {
    // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
