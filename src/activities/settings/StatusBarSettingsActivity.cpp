#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem {
  ITEM_CHAPTER_PAGE_COUNT = 0,
  ITEM_BOOK_PROGRESS_PERCENTAGE,
  ITEM_PROGRESS_BAR,
  ITEM_PROGRESS_BAR_THICKNESS,
  ITEM_TITLE,
  ITEM_BATTERY,
  ITEM_XTC_STATUS_BAR,
  ITEM_COUNT
};

static_assert(ITEM_COUNT == StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS,
              "keep StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS in sync with ITEM_COUNT");

const StrId menuNames[ITEM_COUNT] = {
    StrId::STR_CHAPTER_PAGE_COUNT,
    StrId::STR_BOOK_PROGRESS_PERCENTAGE,
    StrId::STR_PROGRESS_BAR,
    StrId::STR_PROGRESS_BAR_THICKNESS,
    StrId::STR_TITLE,
    StrId::STR_BATTERY,
    StrId::STR_XTC_STATUS_BAR,
};
constexpr int PROGRESS_BAR_ITEMS = 3;
const StrId progressBarNames[PROGRESS_BAR_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

constexpr int PROGRESS_BAR_THICKNESS_ITEMS = 3;
const StrId progressBarThicknessNames[PROGRESS_BAR_THICKNESS_ITEMS] = {
    StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK};

constexpr int TITLE_ITEMS = 3;
const StrId titleNames[TITLE_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

constexpr int XTC_STATUS_BAR_ITEMS = 3;
const StrId xtcStatusBarNames[XTC_STATUS_BAR_ITEMS] = {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP};

const int verticalPreviewTextPadding = 40;
}  // namespace

StatusBarSettingsActivity::StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("StatusBarSettings", renderer, mappedInput) {}

void StatusBarSettingsActivity::onEnter() {
  UiListActivity::onEnter();

  // Clamp statusBarProgressBar and statusBarTitle in case of corrupt/migrated data
  if (SETTINGS.statusBarProgressBar >= PROGRESS_BAR_ITEMS) {
    SETTINGS.statusBarProgressBar = CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;
  }

  if (SETTINGS.statusBarProgressBarThickness >= PROGRESS_BAR_THICKNESS_ITEMS) {
    SETTINGS.statusBarProgressBarThickness = CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS::PROGRESS_BAR_NORMAL;
  }

  if (SETTINGS.statusBarTitle >= TITLE_ITEMS) {
    SETTINGS.statusBarTitle = CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE;
  }

  if (SETTINGS.xtcStatusBarMode >= XTC_STATUS_BAR_ITEMS) {
    SETTINGS.xtcStatusBarMode = CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_HIDE;
  }

  // Labels never change (unlike the values, which track live SETTINGS
  // state), so they're set once here rather than every buildScreen() call.
  for (int i = 0; i < MAX_STATUS_BAR_ITEMS; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

bool StatusBarSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void StatusBarSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  nav.selected = index;
  // Activation opens a popup/sub-activity or repaints a new value; a lingering
  // flash would gray an unrelated row.
  app.clearTapFlash();
  handleSelection();
  requestUpdate();
}

void StatusBarSettingsActivity::handleSelection() {
  switch (nav.selected) {
    case ITEM_CHAPTER_PAGE_COUNT:
      SETTINGS.statusBarChapterPageCount = (SETTINGS.statusBarChapterPageCount + 1) % 2;
      break;
    case ITEM_BOOK_PROGRESS_PERCENTAGE:
      SETTINGS.statusBarBookProgressPercentage = (SETTINGS.statusBarBookProgressPercentage + 1) % 2;
      break;
    case ITEM_PROGRESS_BAR:
      optionPopup.show(StrId::STR_PROGRESS_BAR, progressBarNames, PROGRESS_BAR_ITEMS, SETTINGS.statusBarProgressBar,
                       [this](int idx) {
                         SETTINGS.statusBarProgressBar = idx;
                         SETTINGS.saveToFile();
                       });
      return;
    case ITEM_PROGRESS_BAR_THICKNESS:
      optionPopup.show(StrId::STR_PROGRESS_BAR_THICKNESS, progressBarThicknessNames, PROGRESS_BAR_THICKNESS_ITEMS,
                       SETTINGS.statusBarProgressBarThickness, [this](int idx) {
                         SETTINGS.statusBarProgressBarThickness = idx;
                         SETTINGS.saveToFile();
                       });
      return;
    case ITEM_TITLE:
      optionPopup.show(StrId::STR_TITLE, titleNames, TITLE_ITEMS, SETTINGS.statusBarTitle, [this](int idx) {
        SETTINGS.statusBarTitle = idx;
        SETTINGS.saveToFile();
      });
      return;
    case ITEM_BATTERY:
      SETTINGS.statusBarBattery = (SETTINGS.statusBarBattery + 1) % 2;
      break;
    case ITEM_XTC_STATUS_BAR:
      optionPopup.show(StrId::STR_XTC_STATUS_BAR, xtcStatusBarNames, XTC_STATUS_BAR_ITEMS, SETTINGS.xtcStatusBarMode,
                       [this](int idx) {
                         SETTINGS.xtcStatusBarMode = idx;
                         SETTINGS.saveToFile();
                       });
      return;
    default:
      return;
  }
  SETTINGS.saveToFile();
}

std::string StatusBarSettingsActivity::rowValueText(const int index) {
  switch (index) {
    case ITEM_CHAPTER_PAGE_COUNT:
      return SETTINGS.statusBarChapterPageCount ? tr(STR_SHOW) : tr(STR_HIDE);
    case ITEM_BOOK_PROGRESS_PERCENTAGE:
      return SETTINGS.statusBarBookProgressPercentage ? tr(STR_SHOW) : tr(STR_HIDE);
    case ITEM_PROGRESS_BAR:
      return I18N.get(progressBarNames[SETTINGS.statusBarProgressBar]);
    case ITEM_PROGRESS_BAR_THICKNESS:
      return I18N.get(progressBarThicknessNames[SETTINGS.statusBarProgressBarThickness]);
    case ITEM_TITLE:
      return I18N.get(titleNames[SETTINGS.statusBarTitle]);
    case ITEM_BATTERY:
      return SETTINGS.statusBarBattery ? tr(STR_SHOW) : tr(STR_HIDE);
    case ITEM_XTC_STATUS_BAR:
      return I18N.get(xtcStatusBarNames[SETTINGS.xtcStatusBarMode]);
    default:
      return tr(STR_HIDE);
  }
}

void StatusBarSettingsActivity::buildScreen(UiScreen& screen) {
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_12_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  // Match the main Settings menu: Steinem 11 for names and values.
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Reserve the bottom band for the live status-bar preview footer (label +
  // bar) so the list never runs underneath it, plus the button-hints row below.
  // The preview is pinned directly above the hints (see render()), so the band
  // is just the bar + its label, not a floating gap.
  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  const auto previewFooter =
      static_cast<int16_t>(statusBarHeight + verticalPreviewTextPadding + metrics.verticalSpacing);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 6), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight + previewFooter), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  // rowItems_'s labels/actionValue were set once in onEnter(); only the live
  // value text needs refreshing here, by assigning into the existing
  // rowValues_ strings (no array growth) rather than building a new
  // items/values vector on every render.
  for (int i = 0; i < MAX_STATUS_BAR_ITEMS; i++) {
    rowValues_[i] = rowValueText(i);
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MAX_STATUS_BAR_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  props.rowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listRowHeight + 11);
  props.rowGap = 0;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  props.labelText.lineGap = 6;  // also the explicitly-set marker, see SettingsActivity
  syncListViewport(screen, props);
  screen.list(props);
}

void StatusBarSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the list renders through the app; the preview stays raw.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, pageWidth, metrics.headerHeight},
                 tr(STR_CUSTOMISE_STATUS_BAR));

  renderUi();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  std::string title;
  if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = tr(STR_EXAMPLE_BOOK);
  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_EXAMPLE_CHAPTER);
  }

  // Anchor the preview as a footer directly above the button hints.
  GUI.drawStatusBar(renderer, 75, 8, 32, title, metrics.buttonHintsHeight, 0, false);

  renderer.drawCenteredText(SMALL_FONT_ID,
                            renderer.getScreenHeight() - UITheme::getInstance().getStatusBarHeight() -
                                metrics.buttonHintsHeight - verticalPreviewTextPadding,
                            tr(STR_PREVIEW));

  renderer.displayBuffer();
}
