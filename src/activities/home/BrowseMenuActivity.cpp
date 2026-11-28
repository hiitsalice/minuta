#include "BrowseMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "SettingInfo.h"
#include "SettingsList.h"
#include "activities/ActivityManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/ButtonRemapActivity.h"
#include "activities/settings/ButtonShortcutsActivity.h"
#include "activities/settings/FontDownloadActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
struct SectionDef {
  const char* label;
  int count;
};

// Entries before the first section are always visible.
constexpr int kTopEntryCount = 1;

std::string settingValueText(const SettingInfo& setting) {
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    return SETTINGS.*(setting.valuePtr) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    // Guard like the valueGetter branch below: a corrupt/migrated settings
    // byte must not index past the enum table.
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    if (value >= setting.enumValues.size()) return "";
    return I18N.get(setting.enumValues[value]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    if (value < setting.enumValues.size()) {
      return I18N.get(setting.enumValues[value]);
    }
    return "";
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
      if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
        return tr(STR_SLEEP_NEVER);
      }
      char valueBuffer[32];
      snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
               static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
      return valueBuffer;
    }
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  return "";
}
}  // namespace

BrowseMenuActivity::BrowseMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("BrowseMenu", renderer, mappedInput) {
  allSettings_ = getSettingsList(&sdFontSystem.registry());
  visibleRows_.reserve(MAX_ROWS);
  rowValues_.reserve(MAX_ROWS);
  rowItems_.reserve(MAX_ROWS);
  rebuildVisibleRows();
  rebuildRowItems();
}

namespace {
constexpr SectionDef kSections[5] = {
    {"Files", 2}, {"Reading", 2}, {"Controls", 2}, {"Display", 3}, {"Network", 2},
};
}  // namespace

void BrowseMenuActivity::rebuildVisibleRows() {
  visibleRows_.clear();

  int next = 0;
  for (; next < kTopEntryCount; ++next) {
    VisibleRow row;
    row.entry = static_cast<Entry>(next);
    visibleRows_.push_back(row);
  }

  for (int s = 0; s < SECTION_COUNT; ++s) {
    VisibleRow header;
    header.isSection = true;
    header.section = s;
    visibleRows_.push_back(header);

    for (int i = 0; i < kSections[s].count; ++i) {
      if (sectionExpanded_[s]) {
        VisibleRow row;
        row.section = s;
        row.entry = static_cast<Entry>(next + i);
        visibleRows_.push_back(row);
      }
    }
    next += kSections[s].count;
  }
}

void BrowseMenuActivity::rebuildRowItems() {
  rowValues_.assign(visibleRows_.size(), std::string());
  rowItems_.clear();

  for (size_t i = 0; i < visibleRows_.size(); ++i) {
    const auto& row = visibleRows_[i];
    fui::ListItem item;
    item.actionValue = static_cast<int16_t>(i);

    if (row.isSection) {
      item.label = kSections[row.section].label;
      item.labelXOffset = 21;
      item.bold = true;
      item.leadingTriangleIndicator = true;
      item.triangleDown = sectionExpanded_[row.section];
    } else {
      switch (row.entry) {
        case Entry::HiddenFeatures:
          item.label = I18N.get(StrId::STR_BUTTON_SHORTCUTS);
          item.bold = true;
          break;
        case Entry::Library:
          item.label = I18N.get(StrId::STR_LIBRARY);
          break;
        case Entry::SdFirmwareUpdate:
          item.label = I18N.get(StrId::STR_SD_FIRMWARE_UPDATE);
          break;
        case Entry::RecentBooks:
          item.label = I18N.get(StrId::STR_MENU_RECENT_BOOKS);
          break;
        case Entry::ReaderFont:
          item.label = I18N.get(StrId::STR_FONT_BROWSER);
          break;
        case Entry::RemapFrontButtons:
          item.label = I18N.get(StrId::STR_REMAP_FRONT_BUTTONS);
          break;
        case Entry::ReaderSideButtons:
          item.label = I18N.get(StrId::STR_SIDE_BTN_LAYOUT);
          break;
        case Entry::TimeToSleep:
          item.label = I18N.get(StrId::STR_TIME_TO_SLEEP);
          break;
        case Entry::SleepScreen:
          item.label = I18N.get(StrId::STR_SLEEP_SCREEN);
          break;
        case Entry::RefreshFrequency:
          item.label = I18N.get(StrId::STR_REFRESH_FREQ);
          break;
        case Entry::WifiNetworks:
          item.label = I18N.get(StrId::STR_WIFI_NETWORKS);
          break;
        case Entry::FileTransfer:
          item.label = I18N.get(StrId::STR_FILE_TRANSFER);
          break;
      }
      item.labelXOffset = 0;
    }

    rowItems_.push_back(item);
  }
}

const SettingInfo* BrowseMenuActivity::settingFor(const Entry entry) const {
  StrId id = StrId::STR_NONE_OPT;
  switch (entry) {
    case Entry::ReaderSideButtons:
      id = StrId::STR_SIDE_BTN_LAYOUT;
      break;
    case Entry::TimeToSleep:
      id = StrId::STR_TIME_TO_SLEEP;
      break;
    case Entry::SleepScreen:
      id = StrId::STR_SLEEP_SCREEN;
      break;
    case Entry::RefreshFrequency:
      id = StrId::STR_REFRESH_FREQ;
      break;
    default:
      return nullptr;
  }
  const auto it = std::find_if(allSettings_.begin(), allSettings_.end(),
                               [id](const SettingInfo& setting) { return setting.nameId == id; });
  return it == allSettings_.end() ? nullptr : &*it;
}

int BrowseMenuActivity::listCount() const { return static_cast<int>(visibleRows_.size()); }

const char* BrowseMenuActivity::headerTitle() const { return "Menu"; }

void BrowseMenuActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, renderer.getScreenWidth(), metrics.headerHeight},
                 headerTitle());
}

void BrowseMenuActivity::cycleSetting(const SettingInfo& setting) {
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else {
    return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

void BrowseMenuActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
          StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void BrowseMenuActivity::activateIndex(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= visibleRows_.size()) return;

  app.clearTapFlash();
  nav.selected = index;

  const auto row = visibleRows_[static_cast<size_t>(index)];
  if (row.isSection) {
    sectionExpanded_[row.section] = !sectionExpanded_[row.section];
    rebuildVisibleRows();
    rebuildRowItems();
    nav.selected = std::min(nav.selected, static_cast<int>(visibleRows_.size()) - 1);
    nav.followOnBuild = true;
    requestUpdate();
    return;
  }

  const auto saveAndRefresh = [this](const ActivityResult&) {
    SETTINGS.saveToFile();
    requestUpdate();
  };

  switch (row.entry) {
    case Entry::HiddenFeatures:
      startActivityForResult(std::make_unique<ButtonShortcutsActivity>(renderer, mappedInput), saveAndRefresh);
      break;
    case Entry::Library:
      activityManager.pushActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput));
      break;
    case Entry::RecentBooks:
      activityManager.pushActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput));
      break;
    case Entry::ReaderFont:
      startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput), saveAndRefresh);
      break;
    case Entry::SdFirmwareUpdate:
      startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), saveAndRefresh);
      break;
    case Entry::RemapFrontButtons:
      startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), saveAndRefresh);
      break;
    case Entry::TimeToSleep:
      openSleepTimeoutPicker();
      break;
    case Entry::ReaderSideButtons:
    case Entry::SleepScreen:
    case Entry::RefreshFrequency:
      if (const auto* setting = settingFor(row.entry)) {
        cycleSetting(*setting);
      }
      break;
    case Entry::WifiNetworks:
      startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), saveAndRefresh);
      break;
    case Entry::FileTransfer:
      activityManager.goToFileTransfer();
      break;
  }
}

void BrowseMenuActivity::buildScreen(UiScreen& screen) {
  // Same 10pt text as the Settings list this menu replaces.
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 3), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  // Refresh the live value text (rows keep their label/structure).
  for (size_t i = 0; i < visibleRows_.size(); ++i) {
    const auto& row = visibleRows_[i];
    if (row.isSection) {
      rowItems_[i].value = nullptr;
      rowItems_[i].bold = true;
      rowItems_[i].triangleIndicator = true;
      rowItems_[i].triangleDown = sectionExpanded_[row.section];
      continue;
    }

    const auto* setting = settingFor(row.entry);
    rowValues_[i] = setting ? settingValueText(*setting) : std::string();
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  props.rowGap = 0;
  props.valueInset = 3;  // air between the value and the row edge
  // Labels and values share one size, left-aligned like the Settings list.
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  props.labelText.lineGap = 6;
  syncListViewport(screen, props);
  screen.list(props);
}
