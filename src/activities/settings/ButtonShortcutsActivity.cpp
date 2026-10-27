#include "ButtonShortcutsActivity.h"

#include "CrossPointSettings.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <iterator>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// level 1 = the button to hold or press, level 2 = what it does.
struct Entry {
  int level;
  const char* text;
  const char* boldTail;
  bool currentTheme = false;
  bool currentTool = false;
};

constexpr Entry kGeneral[] = {{1, "Hold POWER", nullptr},
                              {2, "Sleep", nullptr},
                              {1, "Press POWER", nullptr},
                              {2, "Refresh screen", nullptr}};
constexpr Entry kHome[] = {{1, "Hold READ", nullptr},
                           {2, "Switch theme | Currently:", nullptr, true},
                           {1, "Hold MENU", nullptr},
                           {2, "Clear all reading caches", nullptr}};
constexpr Entry kLibrary[] = {{1, "Hold MENU", nullptr},
                              {2, "Show/hide hidden files", nullptr},
                              {1, "Hold SELECT", nullptr},
                              {2, "Delete file |", " No prompt"}};
constexpr Entry kRecent[] = {{1, "Hold SELECT", nullptr}, {2, "Remove from list |", " No prompt"}};
constexpr Entry kReader[] = {{1, "Hold SELECT", nullptr},
                             {2, "Use tool:", nullptr, false, true},
                             {1, "Hold BACK", nullptr},
                             {2, "To main menu", nullptr},
                             {1, "Hold PREV", nullptr},
                             {2, "To previous chapter", nullptr},
                             {1, "Hold NEXT", nullptr},
                             {2, "To next chapter", nullptr}};
constexpr Entry kSettings[] = {{1, "Hold UP", nullptr},
                               {2, "To previous section", nullptr},
                               {1, "Hold DOWN", nullptr},
                               {2, "To next section", nullptr}};
constexpr Entry kReaderMenu[] = {{1, "Hold UP", nullptr},
                                 {2, "To previous section", nullptr},
                                 {1, "Hold DOWN", nullptr},
                                 {2, "To next section", nullptr}};
constexpr Entry kHighlight[] = {{1, "Hold SELECT", nullptr}, {2, "Delete highlight |", " No prompt"}};
constexpr Entry kDictionary[] = {{1, "Hold SELECT on word beside - or \xE2\x80\x93 or \xE2\x80\x94", nullptr},
                                 {2, "Select the compound expression", nullptr}};
constexpr Entry kHiddenFeatures[] = {{1, "Hold UP", nullptr},
                                     {2, "To previous section", nullptr},
                                     {1, "Hold DOWN", nullptr},
                                     {2, "To next section", nullptr}};
constexpr Entry kWifi[] = {{1, "Symbol: +", nullptr},
                           {2, "Saved Wi-Fi", nullptr},
                           {1, "Symbol: *", nullptr},
                           {2, "Encrypted Wi-Fi", nullptr}};
constexpr Entry kOpds[] = {{1, "Add /opds to URL", nullptr},
                           {2, "Use Calibre", nullptr}};

struct SectionDef {
  const char* title;
  const Entry* entries;
  int count;
};

constexpr SectionDef kSections[] = {
    {"General", kGeneral, static_cast<int>(std::size(kGeneral))},
    {"Home", kHome, static_cast<int>(std::size(kHome))},
    {"Settings", kSettings, static_cast<int>(std::size(kSettings))},
    {"Library", kLibrary, static_cast<int>(std::size(kLibrary))},
    {"Recent Book(s)", kRecent, static_cast<int>(std::size(kRecent))},
    {"Reader", kReader, static_cast<int>(std::size(kReader))},
    {"Reader Menu", kReaderMenu, static_cast<int>(std::size(kReaderMenu))},
    {"Highlight List", kHighlight, static_cast<int>(std::size(kHighlight))},
    {"Dictionary", kDictionary, static_cast<int>(std::size(kDictionary))},
    {"Hidden Features", kHiddenFeatures, static_cast<int>(std::size(kHiddenFeatures))},
    {"Wi-Fi Networks", kWifi, static_cast<int>(std::size(kWifi))},
    {"OPDS Server", kOpds, static_cast<int>(std::size(kOpds))},
};
constexpr int kSectionCount = static_cast<int>(std::size(kSections));

constexpr int16_t kSectionX = 21;  // same as the section rows in Settings
constexpr int16_t kTriggerX = 36;
constexpr int16_t kActionX = 72;
}  // namespace

ButtonShortcutsActivity::ButtonShortcutsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ButtonShortcuts", renderer, mappedInput) {}

const char* ButtonShortcutsActivity::headerTitle() const { return tr(STR_BUTTON_SHORTCUTS); }

void ButtonShortcutsActivity::onEnter() {
  expanded_.assign(kSectionCount, 0);  // every section starts closed, like Settings
  rebuildRows();
  UiListActivity::onEnter();
  nav.selected = 0;
  requestUpdate();
}

void ButtonShortcutsActivity::rebuildRows() {
  rows_.clear();
  items_.clear();
  for (int s = 0; s < kSectionCount; ++s) {
    rows_.push_back({Level::Section, s, kSections[s].title, ""});
    if (!expanded_[s]) continue;
    for (int i = 0; i < kSections[s].count; ++i) {
      const Entry& e = kSections[s].entries[i];
      std::string boldTail = e.boldTail ? e.boldTail : "";
      if (e.currentTheme) {
        const char* themeName =
            SETTINGS.uiTheme == CrossPointSettings::UI_THEME::QUARTUM ? "Quartum" : "Solum";
        boldTail = std::string(" ") + themeName;
      }
      if (e.currentTool) {
        const char* toolName = tr(STR_DICTIONARY);
        switch (SETTINGS.longPressMenuFunction) {
          case CrossPointSettings::LP_MENU_HIGHLIGHT:
            toolName = tr(STR_HIGHLIGHT);
            break;
          case CrossPointSettings::LP_MENU_SCREENSHOT:
            toolName = tr(STR_SCREENSHOT_BUTTON);
            break;
          case CrossPointSettings::LP_MENU_DICTIONARY:
          default:
            break;
        }
        boldTail = std::string(" ") + toolName;
      }
      rows_.push_back({e.level == 1 ? Level::Trigger : Level::Action, s, e.text, boldTail,
                       e.currentTool});
    }
  }

  items_.reserve(rows_.size());
  for (size_t i = 0; i < rows_.size(); ++i) {
    fui::ListItem item;
    item.label = rows_[i].text.c_str();
    item.labelBoldTail = rows_[i].boldTail.empty() ? nullptr : rows_[i].boldTail.c_str();
    item.actionValue = static_cast<int16_t>(i);
    switch (rows_[i].level) {
      case Level::Section:
        item.labelXOffset = kSectionX;
        item.bold = true;
        item.leadingTriangleIndicator = true;
        item.triangleDown = expanded_[rows_[i].section] != 0;
        break;
      case Level::Trigger:
        item.labelXOffset = kTriggerX;
        item.leaderLine = true;
        break;
      case Level::Action:
        item.labelXOffset = kActionX;
        item.leaderLine = true;
        break;
    }
    items_.push_back(item);
  }
}

void ButtonShortcutsActivity::navigateButtons() {
  const int count = listCount();
  if (count <= 0) return;

  // Taps move one row, as in an ordinary list.
  buttonNavigator.onNextRelease(
      [this, count] { moveSelectionTo(ButtonNavigator::nextIndex(activeNav().selected, count)); });
  buttonNavigator.onPreviousRelease(
      [this, count] { moveSelectionTo(ButtonNavigator::previousIndex(activeNav().selected, count)); });

  // Holds jump between section headings.
  buttonNavigator.onNextContinuous([this] { jumpToAdjacentSection(1); });
  buttonNavigator.onPreviousContinuous([this] { jumpToAdjacentSection(-1); });
}

void ButtonShortcutsActivity::jumpToAdjacentSection(const int direction) {
  std::vector<size_t> headers;
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].level == Level::Section) headers.push_back(i);
  }
  if (headers.empty()) return;

  const int selected = activeNav().selected;
  size_t target = 0;

  if (selected < 0) {
    target = direction > 0 ? 0 : headers.size() - 1;
  } else {
    if (static_cast<size_t>(selected) >= rows_.size()) return;

    size_t position = 0;
    for (size_t h = 0; h < headers.size(); ++h) {
      if (headers[h] <= static_cast<size_t>(selected)) {
        position = h;
      } else {
        break;
      }
    }

    const bool onHeader =
        rows_[static_cast<size_t>(selected)].level == Level::Section;

    if (direction < 0 && !onHeader) {
      // First reach the current section heading.
      target = position;
    } else {
      // Move to the adjacent section heading, wrapping at either end.
      target = direction > 0 ? (position + 1) % headers.size()
                             : (position + headers.size() - 1) % headers.size();
    }
  }

  moveSelectionTo(static_cast<int>(headers[target]));
}

void ButtonShortcutsActivity::buildScreen(UiScreen& screen) {
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 3), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  fui::ListProps props;
  props.items = items_.data();
  props.count = static_cast<uint16_t>(items_.size());
  props.action = ACTION_ROW;
  props.valueInset = 3;
  props.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  props.rowGap = 0;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  props.labelText.lineGap = 6;
  syncListViewport(screen, props);
  screen.list(props);
}

// Section headings expand/collapse; the tool value row cycles the reader's long-press tool.
void ButtonShortcutsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index < 0 || index >= static_cast<int>(rows_.size())) return;

  if (rows_[index].toolSelector) {
    SETTINGS.longPressMenuFunction = static_cast<uint8_t>(
        (SETTINGS.longPressMenuFunction + 1) %
        CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT);
    SETTINGS.saveToFile();
    {
      RenderLock lock(*this);
      rebuildRows();
      nav.selected = index;
      nav.followOnBuild = true;
    }
    requestUpdate();
    return;
  }

  if (rows_[index].level != Level::Section) return;

  const int section = rows_[index].section;
  {
    RenderLock lock(*this);
    expanded_[section] = expanded_[section] ? 0 : 1;
    rebuildRows();
    nav.selected = index;
    nav.followOnBuild = true;
  }
  requestUpdate();
}

void ButtonShortcutsActivity::drawFooter() {
  const bool validSelection = nav.selected >= 0 && nav.selected < static_cast<int>(rows_.size());
  const bool onSection = validSelection && rows_[nav.selected].level == Level::Section;
  const bool onToolSelector = validSelection && rows_[nav.selected].toolSelector;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), (onSection || onToolSelector) ? tr(STR_SELECT) : "",
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4,
                      -1, drawButtonHintDivider() && listFillsArea());
}
