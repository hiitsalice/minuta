#include "RecentBooksActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <functional>
#include <memory>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
// Hold threshold for the long-press "remove from list" action (firmware convention).
constexpr unsigned long LONG_PRESS_MS = 700;
}  // namespace

RecentBooksActivity::RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("RecentBooks", renderer, mappedInput) {}

void RecentBooksActivity::loadRecentBooks() {
  recentBooks = RECENT_BOOKS.getBooks();
  rebuildRowItems();
}

// Derives rowItems from recentBooks. Called whenever recentBooks changes
// (loadRecentBooks(), i.e. load/removal) so buildScreen() reuses the cached
// rows on every repaint instead of rebuilding them per render.
void RecentBooksActivity::rebuildRowItems() {
  rowItems.clear();
  rowItems.reserve(recentBooks.size());
  for (const auto& book : recentBooks) {
    fui::ListItem item;
    item.label = book.title.c_str();
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }

  // One SD pass for every CJK title/author on the screen; repaints then hit
  // the resident tables instead of re-reading per-string. Titles draw bold
  // (see buildScreen), authors regular — separate per-style prewarms. Getter
  // form: no concatenated copy (a bare-new string append aborts under heap
  // pressure). See GfxRenderer::prewarmFallbackText().
  const auto count = static_cast<uint32_t>(recentBooks.size());
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        return (*static_cast<const std::vector<RecentBook>*>(ctx))[i].title.c_str();
      },
      &recentBooks, count, EpdFontFamily::BOLD);
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        return (*static_cast<const std::vector<RecentBook>*>(ctx))[i].author.c_str();
      },
      &recentBooks, count);
}

void RecentBooksActivity::onEnter() {
  UiListActivity::onEnter();

  // Prune entries whose backing files are gone; this is one of two interaction
  // points where the persistent store gets cleaned (the other is addBook).
  if (RECENT_BOOKS.pruneMissing()) {
    RECENT_BOOKS.saveToFile();
  }

  loadRecentBooks();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  // rowItems' label/subtitle pointers alias recentBooks' strings; drop both.
  rowItems.clear();
  recentBooks.clear();
}

void RecentBooksActivity::activateIndex(const int index) {
  // The interaction table can deliver a row index captured before a removal
  // shrank the list; the next render re-registers the rows.
  if (index < 0 || index >= listCount()) return;
  // Opening the book leaves this screen; a lingering flash would gray an
  // unrelated row when the list next appears.
  app.clearTapFlash();
  LOG_DBG("RBA", "Selected recent book: %s", recentBooks[index].path.c_str());
  onSelectBook(recentBooks[index].path);
}

void RecentBooksActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  // Long-press prompts removal from the list (mirrors the Confirm-button hold).
  app.clearTapFlash();
  promptRemoveBook(recentBooks[index].path, recentBooks[index].title);
}

bool RecentBooksActivity::handleButtons() {
  if (!recentBooks.empty() && nav.selected < listCount() &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, LONG_PRESS_MS)) {
    promptRemoveBook(recentBooks[nav.selected].path, recentBooks[nav.selected].title);
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!recentBooks.empty() && nav.selected < listCount()) {
      if (mappedInput.getHeldTime() >= LONG_PRESS_MS) {
        promptRemoveBook(recentBooks[nav.selected].path, recentBooks[nav.selected].title);
      } else {
        activateIndex(nav.selected);
      }
      return true;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return true;
  }

  return false;
}

void RecentBooksActivity::promptRemoveBook(const std::string& path, const std::string& title) {
  auto handler = [this, path](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("RBA", "Remove from recents cancelled");
      return;
    }
    if (RECENT_BOOKS.removeByPath(path)) {
      // Also drop the book's cache so it reopens at page one. Highlights live
      // in /.crosspoint/bookmarks/ and are not touched.
      const std::string pathHash = std::to_string(std::hash<std::string>{}(path));
      for (const char* prefix : {"/.crosspoint/epub_", "/.crosspoint/txt_"}) {
        const std::string cacheDir = std::string(prefix) + pathHash;
        if (Storage.exists(cacheDir.c_str())) Storage.removeDir(cacheDir.c_str());
      }
      LOG_DBG("RBA", "Removed from recents: %s", path.c_str());
      // The interaction table still indexes the pre-removal rows; stop routing
      // touches against it until the next render republishes.
      closeRouting();
      loadRecentBooks();
      if (recentBooks.empty()) {
        nav.selected = 0;
      } else if (nav.selected >= listCount()) {
        nav.selected = listCount() - 1;
      }
      nav.follow(listCount());
      requestUpdate(true);
    }
  };

  (void)title;
  ActivityResult res;
  res.isCancelled = false;
  handler(res);
}

void RecentBooksActivity::buildScreen(UiScreen& screen) {
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  const auto& metrics = UITheme::getInstance().getMetrics();
  // Same margins and spacing as the Library.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 9), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight + 3), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));
  screen.spacer(10);

  if (recentBooks.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, 420, tr(STR_NO_RECENT_BOOKS));
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  fui::TextStyle label = screen.theme().smallText;
  label.maxLines = 1;
  label.align = fui::TextAlign::Center;
  props.labelText = label;
  props.balanceWrappedLabelWithValue = false;
  props.partialTrailingRow = false;
  props.rowHeight = 36;
  props.labelYOffset = 1;
  props.rowGap = 0;
  syncListViewport(screen, props, false, 36);
  screen.list(props, static_cast<int16_t>(screen.body().height - 36));
}

void RecentBooksActivity::drawFooter() {
  const bool empty = recentBooks.empty();
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), empty ? "" : tr(STR_SELECT), empty ? "" : tr(STR_DIR_UP),
                                            empty ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (empty) return;

  // Same box and position as "Hold SELECT to delete" in the highlight list.
  const char* helpText = "Hold SELECT to delete";
  const int helpLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textHeight = renderer.getTextHeight(UI_10_FONT_ID);
  const int hintTop = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, helpText);
  const int textX = (renderer.getScreenWidth() - textWidth) / 2;
  const int textY = hintTop - 15 - helpLineHeight + 2;

  constexpr int boxPadding = 6;
  renderer.fillRectDither(textX - boxPadding, textY - boxPadding, textWidth + boxPadding * 2,
                          textHeight + boxPadding * 2, Color::LightGray);
  renderer.drawText(UI_10_FONT_ID, textX, textY, helpText, true, EpdFontFamily::REGULAR);
}
