#include "ButtonShortcutsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <iterator>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr const char* kGeneral[] = {"Hold POWER: Sleep", "Press POWER: Refresh screen"};
constexpr const char* kHome[] = {"Hold READ: Switch theme", "Hold BACK: Clear all reading caches"};
constexpr const char* kLibrary[] = {"Hold MENU: Show hidden files", "Hold SELECT: Delete file immediately"};
constexpr const char* kRecent[] = {"Hold SELECT: Remove from list immediately"};
constexpr const char* kReader[] = {"Hold BACK: Open main menu", "Hold PREV: Previous chapter",
                                   "Hold NEXT: Next chapter"};
constexpr const char* kHighlight[] = {"Hold SELECT: Delete highlight immediately"};
// One note, three display lines: it takes the height of three ordinary rows.
constexpr const char* kDictionary[] = {"Hold SELECT on a word beside", "a hyphen, en dash or em dash:",
                                       "Select the compound expression"};
constexpr const char* kWifi[] = {"+ = Saved Wi-Fi", "* = Encrypted Wi-Fi"};
constexpr const char* kOpds[] = {"For Calibre, add /opds to your URL"};

struct SectionDef {
  const char* title;
  const char* const* lines;
  int count;
};

constexpr SectionDef kSections[] = {
    {"General", kGeneral, static_cast<int>(std::size(kGeneral))},
    {"Home", kHome, static_cast<int>(std::size(kHome))},
    {"Library", kLibrary, static_cast<int>(std::size(kLibrary))},
    {"Recent Books", kRecent, static_cast<int>(std::size(kRecent))},
    {"Reader", kReader, static_cast<int>(std::size(kReader))},
    {"Highlight List", kHighlight, static_cast<int>(std::size(kHighlight))},
    {"Dictionary", kDictionary, static_cast<int>(std::size(kDictionary))},
    {"Wi-Fi Networks", kWifi, static_cast<int>(std::size(kWifi))},
    {"OPDS Server", kOpds, static_cast<int>(std::size(kOpds))},
};
constexpr int kSectionCount = static_cast<int>(std::size(kSections));
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
    rows_.push_back({true, s, kSections[s].title});
    if (expanded_[s]) {
      for (int i = 0; i < kSections[s].count; ++i) {
        rows_.push_back({false, s, kSections[s].lines[i]});
      }
    }
  }
  items_.reserve(rows_.size());
  for (size_t i = 0; i < rows_.size(); ++i) {
    fui::ListItem item;
    item.label = rows_[i].text;
    item.actionValue = static_cast<int16_t>(i);
    if (rows_[i].isHeader) {
      item.labelXOffset = 21;
      item.bold = true;
      item.leadingTriangleIndicator = true;
      item.triangleDown = expanded_[rows_[i].section] != 0;
    }
    items_.push_back(item);
  }
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
  props.labelText.maxLines = 1;  // one line per row, so every row is exactly one row high
  props.labelText.lineGap = 6;
  syncListViewport(screen, props);
  screen.list(props);
}

void ButtonShortcutsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index < 0 || index >= static_cast<int>(rows_.size())) return;

  if (!rows_[index].isHeader) {
    // Notes are read-only: a stray tap goes back to the section the note belongs to.
    int h = index;
    while (h > 0 && !rows_[h].isHeader) --h;
    nav.selected = h;
    requestUpdate();
    return;
  }

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

// Only the section rows can be landed on, so every direction button steps to the neighbouring section.
void ButtonShortcutsActivity::moveToHeader(const int direction) {
  std::vector<int> headers;
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].isHeader) headers.push_back(static_cast<int>(i));
  }
  if (headers.empty()) return;

  const int n = static_cast<int>(headers.size());
  int pos = 0;
  for (int h = 0; h < n; ++h) {
    if (headers[h] <= nav.selected) pos = h;
  }
  const int next = (pos + direction + n) % n;
  {
    RenderLock lock(*this);
    nav.selected = headers[next];
    nav.follow(listCount());
  }
  requestUpdate();
}

void ButtonShortcutsActivity::navigateButtons() {
  using Button = MappedInputManager::Button;
  navigator_.onRelease({Button::Down, Button::Right}, [this] { moveToHeader(1); });
  navigator_.onRelease({Button::Up, Button::Left}, [this] { moveToHeader(-1); });
  navigator_.onContinuous({Button::Down, Button::Right}, [this] { moveToHeader(1); });
  navigator_.onContinuous({Button::Up, Button::Left}, [this] { moveToHeader(-1); });
}

void ButtonShortcutsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
