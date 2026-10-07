#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks, bool* categoryExpanded, int* selectedRow)
    : UiListActivity("EpubReaderMenu", renderer, mappedInput),
      title(title),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent),
      categoryExpanded(categoryExpanded),
      selectedRow(selectedRow) {
  buildMenuItems(menuItems, hasFootnotes, hasBookmarks);
  rebuildVisibleRows();
  if (selectedRow && *selectedRow >= 0 && *selectedRow < static_cast<int>(visibleRows.size()))
    nav.selected = *selectedRow;
}

void EpubReaderMenuActivity::onEnter() {
  UiListActivity::onEnter();
  if (selectedRow && *selectedRow >= 0 && *selectedRow < static_cast<int>(visibleRows.size()))
    nav.selected = *selectedRow;
}

void EpubReaderMenuActivity::rebuildVisibleRows() {
  visibleRows.clear();

  constexpr size_t NAVIGATION_START = 0;
  constexpr size_t NAVIGATION_END = 4;
  constexpr size_t TOOLS_START = 4;
  constexpr size_t TOOLS_END = 9;
  constexpr size_t APPEARANCE_START = 9;
  constexpr size_t APPEARANCE_END = 12;
  constexpr size_t UTILITIES_START = 12;
  constexpr size_t UTILITIES_END = 15;

  const auto addCategory = [this](const Category category, const size_t start, const size_t end) {
    const size_t categoryIndex = static_cast<size_t>(category);
    visibleRows.push_back({true, category, 0});

    if (!categoryExpanded[categoryIndex]) return;

    for (size_t i = start; i < end; ++i)
      visibleRows.push_back({false, category, i});
  };

  addCategory(Category::NAVIGATION, NAVIGATION_START, NAVIGATION_END);
  addCategory(Category::TOOLS, TOOLS_START, TOOLS_END);
  addCategory(Category::APPEARANCE, APPEARANCE_START, APPEARANCE_END);
  addCategory(Category::UTILITIES, UTILITIES_START, UTILITIES_END);

  buildMenuRowItems();
}

void EpubReaderMenuActivity::buildMenuRowItems() {
  for (size_t i = 0; i < MAX_MENU_ITEMS; ++i)
    menuRowItems[i] = {};

  static constexpr const char* CATEGORY_LABELS[] = {
      "Navigation",
      "Tools",
      "Appearance",
      "Utilities",
  };

  for (size_t i = 0; i < visibleRows.size() && i < MAX_MENU_ITEMS; ++i) {
    const auto& row = visibleRows[i];
    fui::ListItem item;

    if (row.isCategory) {
      const size_t categoryIndex = static_cast<size_t>(row.category);
      item.label = CATEGORY_LABELS[categoryIndex];
      item.bold = true;
      item.labelXOffset = 21;
      item.leadingTriangleIndicator = true;
      item.triangleDown = categoryExpanded[categoryIndex];
    } else {
      item.label = menuItems[row.menuIndex].action == MenuAction::DICTIONARY
                       ? "Dictionary"
                       : I18N.get(menuItems[row.menuIndex].labelId);
    }

    item.actionValue = static_cast<int16_t>(i);
    menuRowItems[i] = item;
  }
}

void EpubReaderMenuActivity::buildMenuItems(std::vector<MenuItem>& items, bool hasFootnotes, bool hasBookmarks) {
  items.clear();
  items.reserve(15);

  (void)hasFootnotes;  // Endnote List is always listed.
  (void)hasBookmarks;  // Highlight List is always listed.

  // Navigation
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_SEC_PER_PAGE});

  // Tools
  items.push_back({MenuAction::HIGHLIGHT, StrId::STR_HIGHLIGHT});
  items.push_back({MenuAction::HIGHLIGHT_MARKER, StrId::STR_HIGHLIGHT_MARKER});
  items.push_back({MenuAction::BOOKMARKS, StrId::STR_HIGHLIGHT_LIST});
  items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});

  // Appearance
  items.push_back({MenuAction::STATUS_BAR, StrId::STR_CUSTOMISE_STATUS_BAR});
  items.push_back({MenuAction::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS});
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});

  // Utilities
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
}

void EpubReaderMenuActivity::closeCancelled() {
  if (selectedRow) *selectedRow = nav.selected;
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
  setResult(std::move(result));
  finish();
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  if (index < 0 || index >= static_cast<int>(visibleRows.size())) return;

  // The activated row leaves this screen (popup or finish), or changes the
  // visible list. Clear any lingering tap flash before either case.
  app.clearTapFlash();
  nav.selected = index;

  const auto row = visibleRows[static_cast<size_t>(index)];

  if (row.isCategory) {
    const size_t categoryIndex = static_cast<size_t>(row.category);
    categoryExpanded[categoryIndex] = !categoryExpanded[categoryIndex];

    // The category itself remains selected after the visible-row list changes.
    // Rebuilding from the category index preserves that selection naturally.
    rebuildVisibleRows();
    nav.followOnBuild = true;
    requestUpdate();
    return;
  }

  const auto selectedAction = menuItems[row.menuIndex].action;

  if (selectedAction == MenuAction::ROTATE_SCREEN) {
    pendingOrientation = (pendingOrientation + 1) % static_cast<int>(orientationLabels.size());
    // Rotate the menu immediately. Only the renderer turns;
    // SETTINGS.orientation stays unchanged so the reader's
    // result handler still detects the change and reflows.
    ReaderUtils::applyOrientation(renderer, pendingOrientation);
    app.setDevice(uiTarget.deviceContext());
    nav.followOnBuild = true;
    requestUpdate(true);
    return;
  }

  if (selectedAction == MenuAction::HIGHLIGHT_MARKER) {
    SETTINGS.highlightMarkerEnabled = SETTINGS.highlightMarkerEnabled ? 0 : 1;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
    selectedPageTurnOption = (selectedPageTurnOption + 1) % static_cast<int>(pageTurnLabels.size());
    requestUpdate();
    return;
  }

  if (selectedRow) *selectedRow = nav.selected;
  setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
  finish();
}

bool EpubReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool EpubReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void EpubReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  constexpr int16_t menuEdgePadding = 4;
  constexpr int16_t menuVerticalOffset = 1;
  // Shift the complete menu content upward without changing its height.
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight - menuVerticalOffset),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + menuEdgePadding +
                                       menuVerticalOffset),
                  static_cast<int16_t>(safe.x)});

  // Progress summary where the old sub-header band sat.
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
  const int16_t pad = screen.theme().headerSidePadding;

  // Reader-menu progress summary stays compact at Steinem 10pt.
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  auto progressStyle = screen.theme().smallText;
  progressStyle.align = fui::TextAlign::Right;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), progressStyle);
  screen.spacer(static_cast<int16_t>(menuEdgePadding - 2));

  // Minuta reader menu rows use the same 12pt UI size as the outer menus.
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_12_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  for (size_t i = 0; i < visibleRows.size() && i < MAX_MENU_ITEMS; ++i) {
    const auto& row = visibleRows[i];

    if (row.isCategory) {
      const size_t categoryIndex = static_cast<size_t>(row.category);
      menuRowItems[i].triangleIndicator = true;
      menuRowItems[i].triangleDown = categoryExpanded[categoryIndex];
      menuRowItems[i].value = nullptr;
      continue;
    }

    const auto action = menuItems[row.menuIndex].action;

    if (action == MenuAction::HIGHLIGHT_MARKER) {
      menuRowItems[i].value = SETTINGS.highlightMarkerEnabled ? "On" : "Off";
    } else if (action == MenuAction::ROTATE_SCREEN) {
      menuRowItems[i].value = I18N.get(orientationLabels[pendingOrientation]);
    } else if (action == MenuAction::AUTO_PAGE_TURN) {
      menuRowItems[i].value = pageTurnLabels[selectedPageTurnOption];
    } else {
      menuRowItems[i].value = nullptr;
    }
  }

  fui::ListProps props;
  props.items = menuRowItems;
  props.count = static_cast<uint16_t>(visibleRows.size());
  props.action = ACTION_ROW;
  props.rowHeight = static_cast<int16_t>(metrics.listRowHeight + 11);
  props.rowGap = 0;
  props.valueInset = 8;
  props.labelXOffset = 0;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  props.labelText.lineGap = 6;
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubReaderMenuActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str(), nullptr, false, false);
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  drawChrome();

  renderUi();

  // Wrapped labels can make the actual visible row count smaller than the
  // fixed-height estimate. ListNav may correct the viewport after layout;
  // rebuild so the displayed rows match the corrected selection/viewport.
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }

  drawFooter();
  renderer.displayBuffer();
}
