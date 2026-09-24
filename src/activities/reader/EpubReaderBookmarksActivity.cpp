#include "EpubReaderBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Epub/Section.h>

#include <algorithm>

#include "../../util/BookmarkFile.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int ENTER_DELETE_MODE_MS = 500;
}  // namespace

EpubReaderBookmarksActivity::EpubReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         const std::shared_ptr<Epub>& epub, const std::string& epubPath)
    : UiListActivity("EpubReaderBookmarks", renderer, mappedInput),
      epub(epub),
      epubPath(epubPath) {}

void EpubReaderBookmarksActivity::onEnter() {
  UiListActivity::onEnter();

  if (!epub) {
    return;
  }

  if (!BookmarkFile::load(epubPath, bookmarks, highlights)) {
    bookmarks.shrink_to_fit();
    highlights.shrink_to_fit();
  }
  LOG_DBG("EPB", "Loaded %d bookmarks for book: %s", static_cast<int>(bookmarks.size()), epubPath.c_str());
  rebuildSavedRowItems();
}

// Derives savedSubtitles/savedRowItems from `bookmarks`. Called
// whenever `bookmarks` changes (onEnter() load, post-delete) so buildScreen()
// reuses the cached rows on every repaint instead of re-composing a
// percentage/chapter/TOC-title subtitle string per bookmark each time.
void EpubReaderBookmarksActivity::rebuildSavedRowItems() {
  savedRows.clear();
  savedSubtitles.clear();
  savedRowItems.clear();
  if (!epub) {
    return;
  }

  auto truncateChapterTitle = [](const std::string& title) {
    constexpr size_t MAX_TITLE_LENGTH = 21;
    constexpr size_t ELLIPSIS_LENGTH = 3;

    if (title.size() <= MAX_TITLE_LENGTH) {
      return title;
    }

    const size_t limit = MAX_TITLE_LENGTH;
    size_t end = title.rfind(' ', limit);

    if (end == std::string::npos) {
      return std::string("...");
    }

    return title.substr(0, end) + "...";
  };

  auto makeSubtitle = [&](const std::string& title, float percentage,
                          uint16_t pageProgress, uint16_t pageCount) {
    std::string subtitle = truncateChapterTitle(title);
    subtitle += " - Page ";
    if (pageCount > 0) {
      subtitle += std::to_string(pageProgress + 1) + "/" + std::to_string(pageCount);
    } else {
      subtitle += "?/?";
    }
    subtitle += " - ";
    subtitle += std::to_string(
        static_cast<int>(std::clamp(percentage, 0.0f, 1.0f) * 100.0f + 0.5f));
    subtitle += "%";
    return subtitle;
  };

  savedRows.reserve(bookmarks.size() + highlights.size());
  savedSubtitles.reserve(bookmarks.size() + highlights.size());
  savedRowItems.reserve(bookmarks.size() + highlights.size());

  for (size_t i = 0; i < bookmarks.size(); i++) {
    savedRows.push_back({false, static_cast<int>(i)});
  }

  for (size_t i = 0; i < highlights.size(); i++) {
    savedRows.push_back({true, static_cast<int>(i)});
  }

  std::stable_sort(savedRows.begin(), savedRows.end(), [this](const SavedRow& a, const SavedRow& b) {
    const uint16_t aSpine = a.isHighlight
                                ? highlights[a.index].spineIndex
                                : bookmarks[a.index].computedSpineIndex;
    const uint16_t bSpine = b.isHighlight
                                ? highlights[b.index].spineIndex
                                : bookmarks[b.index].computedSpineIndex;

    if (aSpine != bSpine) {
      return aSpine < bSpine;
    }

    const uint16_t aPage = a.isHighlight
                               ? highlights[a.index].computedChapterProgress
                               : bookmarks[a.index].computedChapterProgress;
    const uint16_t bPage = b.isHighlight
                               ? highlights[b.index].computedChapterProgress
                               : bookmarks[b.index].computedChapterProgress;

    if (aPage != bPage) {
      return aPage < bPage;
    }

    // On the same page, bookmarks always come before highlights.
    if (a.isHighlight != b.isHighlight) {
      return !a.isHighlight;
    }

    // Highlights on the same page are ordered by where their text starts.
    if (a.isHighlight) {
      return highlights[a.index].startVisibleTextOffset <
             highlights[b.index].startVisibleTextOffset;
    }

    // Keep the original order for bookmarks at the same page.
    return false;
  });

  for (const auto& row : savedRows) {
    const std::string* summary = nullptr;
    std::string subtitle;

    if (row.isHighlight) {
      const auto& highlight = highlights[row.index];
      const auto tocIndex = epub->getTocIndexForSpineIndex(highlight.spineIndex);
      const auto tocTitle = (tocIndex >= 0) ? epub->getTocItem(tocIndex).title : tr(STR_UNNAMED);

      subtitle = makeSubtitle(
          tocTitle,
          highlight.percentage,
          highlight.computedChapterProgress,
          highlight.computedChapterPageCount);
      summary = &highlight.summary;
    } else {
      const auto& bookmark = bookmarks[row.index];
      const auto tocIndex = epub->getTocIndexForSpineIndex(bookmark.computedSpineIndex);
      const auto tocTitle = (tocIndex >= 0) ? epub->getTocItem(tocIndex).title : tr(STR_UNNAMED);

      subtitle = makeSubtitle(
          tocTitle,
          bookmark.percentage,
          bookmark.computedChapterProgress,
          bookmark.computedChapterPageCount);
      summary = &bookmark.summary;
    }

    savedSubtitles.push_back(std::move(subtitle));

    fui::ListItem item;
    item.label = summary->c_str();
    item.subtitle = savedSubtitles.back().c_str();
    item.actionValue = static_cast<int16_t>(savedRowItems.size());
    savedRowItems.push_back(item);
  }

}

void EpubReaderBookmarksActivity::openSelectedItem() {
  if (savedRows.empty()) {
    return;
  }

  const auto& row = savedRows.at(nav.selected);
  ProgressChangeResult result{};

  if (row.isHighlight) {
    const auto& highlight = highlights.at(row.index);
    result.spineIndex = highlight.spineIndex;
    result.visibleTextOffset = highlight.startVisibleTextOffset;
    result.hasVisibleTextOffset = true;
    result.hasSavedProgress = false;
  } else {
    const auto& bookmark = bookmarks.at(row.index);
    result.xpath = bookmark.xpath;
    result.percentage = bookmark.percentage;
    result.hasSavedProgress = true;
    result.hasVisibleTextOffset = bookmark.hasVisibleTextOffset;
    result.visibleTextOffset = bookmark.visibleTextOffset;
    result.spineIndex = bookmark.computedSpineIndex;

    if (bookmark.computedChapterPageCount > 0 &&
        bookmark.computedChapterProgress < bookmark.computedChapterPageCount &&
        bookmark.computedSpineIndex < epub->getSpineItemsCount()) {
      result.page = bookmark.computedChapterProgress;
      result.totalPages = bookmark.computedChapterPageCount;
    }
  }

  setResult(std::move(result));
  finish();
}

void EpubReaderBookmarksActivity::activateIndex(const int index) {
  if (confirmPopup.isActive()) return;
  // The interaction table can deliver a row index captured before a delete
  // shrank the list; the next render re-registers the rows.
  if (index < 0 || index >= listCount()) return;
  // The tapped row leaves this screen; a lingering flash would gray an
  // unrelated row on the next render.
  app.clearTapFlash();
  nav.selected = index;
  openSelectedItem();
}

bool EpubReaderBookmarksActivity::handleCustomInput() {
  return false;
}

bool EpubReaderBookmarksActivity::handleButtons() {
  static bool confirmHeld = false;
  static bool confirmLongHandled = false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  if (confirmHeld && !confirmLongHandled &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    deleteSelectedItem();
    confirmLongHandled = true;
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (confirmHeld && !confirmLongHandled) {
      openSelectedItem();
    }
    confirmHeld = false;
    confirmLongHandled = false;
    return true;
  }

  return false;
}


void EpubReaderBookmarksActivity::deleteSelectedItem() {
  if (savedRows.empty()) {
    return;
  }

  const auto row = savedRows.at(nav.selected);
  if (row.isHighlight) {
    highlights.erase(highlights.begin() + row.index);
  } else {
    bookmarks.erase(bookmarks.begin() + row.index);
  }

  rebuildSavedRowItems();

  if (!BookmarkFile::save(epubPath, bookmarks, highlights)) {
    LOG_ERR("EPB", "Failed to save saved items");
  }

  if (savedRows.empty()) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (nav.selected >= static_cast<int>(savedRows.size())) {
    nav.selected = static_cast<int>(savedRows.size()) - 1;
  }

  nav.follow(listCount());
  requestUpdate(true);
}

void EpubReaderBookmarksActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Saved has no header, so the first row starts at the top content area.
  screen.setContentMargin(fui::Insets{12,
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(15 + metrics.buttonHintsHeight),
                                      static_cast<int16_t>(safe.x)});

  if (savedRows.empty()) {
    UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, 404,
                              tr(STR_NO_SAVED));
    return;
  }

  // savedSubtitles/savedRowItems are built once whenever `bookmarks`
  // changes (see rebuildSavedRowItems()) and reused here on every repaint.
  fui::ListProps props;
  props.items = savedRowItems.data();
  props.count = static_cast<uint16_t>(savedRowItems.size());
  props.action = ACTION_ROW;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  props.labelText.lineGap = 1;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 1;
  props.subtitleText.lineGap = 1;
  props.subtitleText.align = fui::TextAlign::Right;
  props.subtitleText.bold = false;
  props.subtitleText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.subtitleGap = 6;
  props.labelYOffset = 1;
  props.rowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listWithSubtitleRowHeight - 2);
  props.rowInset = 18;
  props.sidePadding = 12;
  // Tap opens; long-press deletes (physical buttons stay in loop()).
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void EpubReaderBookmarksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  // Landscape orientation: reserve a horizontal gutter for button hints.
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  // Inverted portrait: reserve vertical space for hints at the top.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 40 : 0;
  // Landscape CW places hints on the left edge; CCW keeps them on the right.
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int contentY = isPortraitInverted ? 50 : 0;

  renderUi();

  if (confirmPopup.processRender(renderer, mappedInput)) return;

  const auto confirmLabel = savedRows.empty() ? "" : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const char* helpText = "Hold SELECT to delete";
  const int helpLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textHeight = renderer.getTextHeight(UI_10_FONT_ID);
  const int hintTop = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, helpText);
  const int textX = (renderer.getScreenWidth() - textWidth) / 2;
  const int textY = hintTop - 15 - helpLineHeight;

  constexpr int boxPadding = 6;
  renderer.fillRectDither(textX - boxPadding,
                          textY - boxPadding,
                          textWidth + boxPadding * 2,
                          textHeight + boxPadding * 2,
                          Color::LightGray);
  renderer.drawText(UI_10_FONT_ID, textX, textY, helpText, true, EpdFontFamily::REGULAR);

  renderer.displayBuffer();
}
