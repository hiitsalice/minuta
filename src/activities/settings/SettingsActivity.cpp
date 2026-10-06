#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ButtonRemapActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "FontDownloadActivity.h"
#include "KOReaderSettingsActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

const char* const SettingsActivity::categoryNames[categoryCount] = {
    "Device",
    "Controls",
    "System",
};

namespace {

constexpr size_t DEVICE_DISPLAY_START = 0;
constexpr size_t DEVICE_DISPLAY_END = 3;
constexpr size_t DEVICE_APPEARANCE_START = 3;
constexpr size_t DEVICE_APPEARANCE_END = 5;
constexpr size_t DEVICE_READING_START = 5;
constexpr size_t DEVICE_READING_END = 7;

constexpr size_t SYSTEM_NETWORK_START = 0;
constexpr size_t SYSTEM_NETWORK_END = 4;
constexpr size_t SYSTEM_UPDATE_START = 4;
constexpr size_t SYSTEM_UPDATE_END = 6;

}  // namespace

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiTabListActivity("Settings", renderer, mappedInput) {}

void SettingsActivity::rebuildSettingsLists() {
  deviceSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  // Rescan /dictionaries on every rebuild: cheap (one directory listing) and
  // picks up dictionaries copied to the SD card since the last visit.
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);

  const auto allSettings = getSettingsList(&sdFontSystem.registry(), &dictionaries);

  const auto appendSetting = [&](std::vector<SettingInfo>& destination, const StrId nameId) {
    const auto it =
        std::find_if(allSettings.begin(), allSettings.end(),
                     [nameId](const SettingInfo& setting) { return setting.nameId == nameId; });
    if (it != allSettings.end()) {
      destination.push_back(*it);
    }
  };

  // Device -> Display
  appendSetting(deviceSettings, StrId::STR_TIME_TO_SLEEP);
  appendSetting(deviceSettings, StrId::STR_SLEEP_SCREEN);
  appendSetting(deviceSettings, StrId::STR_REFRESH_FREQ);

  // Device -> Appearance
  appendSetting(deviceSettings, StrId::STR_UI_THEME);
  appendSetting(deviceSettings, StrId::STR_SUNLIGHT_FADING_FIX);

  // Device -> Reading
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_FONT_BROWSER, SettingAction::DownloadFonts));
  appendSetting(deviceSettings, StrId::STR_DICTIONARY);

  // Controls remains the existing flat list.
  for (const auto& setting : allSettings) {
    if (setting.category == StrId::STR_CAT_CONTROLS) {
      controlsSettings.push_back(setting);
    }
  }

  controlsSettings.insert(
      controlsSettings.begin(),
      SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));

  // System -> Network
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_FILE_TRANSFER, SettingAction::FileTransfer));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));

  // System -> Update
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));

  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &deviceSettings;
      break;
    case 1:
      currentSettings = &controlsSettings;
      break;
    case 2:
      currentSettings = &systemSettings;
      break;
  }

  rebuildVisibleRows();
  rebuildRowItems();
}

void SettingsActivity::rebuildVisibleRows() {
  visibleRows.clear();

  if (selectedCategoryIndex == 1) {
    for (size_t i = 0; i < currentSettings->size(); ++i) {
      visibleRows.push_back({false, Section::DEVICE_DISPLAY, i});
    }
    return;
  }

  auto addSection = [this](const Section section, const size_t start, const size_t end) {
    visibleRows.push_back({true, section, 0});

    const size_t sectionIndex = static_cast<size_t>(section);
    if (!sectionExpanded[selectedCategoryIndex][sectionIndex]) return;

    for (size_t i = start; i < end; ++i) {
      visibleRows.push_back({false, section, i});
    }
  };

  if (selectedCategoryIndex == 0) {
    addSection(Section::DEVICE_DISPLAY, DEVICE_DISPLAY_START, DEVICE_DISPLAY_END);
    addSection(Section::DEVICE_APPEARANCE, DEVICE_APPEARANCE_START, DEVICE_APPEARANCE_END);
    addSection(Section::DEVICE_READING, DEVICE_READING_START, DEVICE_READING_END);
  } else {
    addSection(Section::SYSTEM_NETWORK, SYSTEM_NETWORK_START, SYSTEM_NETWORK_END);
    addSection(Section::SYSTEM_UPDATE, SYSTEM_UPDATE_START, SYSTEM_UPDATE_END);
  }
}

void SettingsActivity::onEnter() {
  UiTabListActivity::onEnter();

  selectedCategoryIndex = 0;
  rebuildSettingsLists();
  requestUpdate();
}

void SettingsActivity::selectCategory(const int categoryIndex) {
  selectedCategoryIndex = categoryIndex;

  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &deviceSettings;
      break;
    case 1:
      currentSettings = &controlsSettings;
      break;
    case 2:
      currentSettings = &systemSettings;
      break;
  }

  activeNav().top = 0;
  activeNav().selected = 0;
  rebuildVisibleRows();
  rebuildRowItems();
}

void SettingsActivity::rebuildRowItems() {
  const auto sectionLabel = [](const Section section) {
    switch (section) {
      case Section::DEVICE_DISPLAY:
        return "Display";
      case Section::DEVICE_APPEARANCE:
        return "Appearance";
      case Section::DEVICE_READING:
        return "Reading";
      case Section::SYSTEM_NETWORK:
        return "Network";
      case Section::SYSTEM_UPDATE:
        return "Update";
    }
    return "";
  };

  rowValues_.assign(visibleRows.size(), std::string());
  rowItems_.clear();
  rowItems_.reserve(visibleRows.size());

  for (size_t i = 0; i < visibleRows.size(); ++i) {
    const auto& row = visibleRows[i];
    fui::ListItem item;
    item.actionValue = static_cast<int16_t>(i);

    if (row.isCategory) {
      item.label = sectionLabel(row.section);
      item.labelXOffset = 21;
      item.bold = true;
      item.leadingTriangleIndicator = true;
      item.triangleDown = sectionExpanded[selectedCategoryIndex][static_cast<size_t>(row.section)];
    } else {
      item.label = I18N.get((*currentSettings)[row.settingIndex].nameId);
      item.labelXOffset = 0;
    }

    rowItems_.push_back(item);
  }
}

void SettingsActivity::onTabAction(const int index) {
  if (optionPopup.isActive()) return;
  selectCategory(index);
  activeNav().selected = 0;
  app.clearTapFlash();
}

void SettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  if (index < 0 || static_cast<size_t>(index) >= visibleRows.size()) return;

  app.clearTapFlash();

  const auto row = visibleRows[static_cast<size_t>(index)];
  if (row.isCategory) {
    const size_t sectionIndex = static_cast<size_t>(row.section);
    sectionExpanded[selectedCategoryIndex][sectionIndex] = !sectionExpanded[selectedCategoryIndex][sectionIndex];
    rebuildVisibleRows();
    rebuildRowItems();
    activeNav().selected = std::min(activeNav().selected, static_cast<int>(visibleRows.size()));
    activeNav().followOnBuild = true;
    requestUpdate();
    return;
  }

  activeNav().selected = index + 1;
  toggleCurrentSetting();
}

void SettingsActivity::stepTab(const int direction) {
  const bool onTabBar = ringPos() == 0;

  selectedCategoryIndex =
      direction > 0 ? ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount)
                    : ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);

  selectCategory(selectedCategoryIndex);
  activeNav().selected = onTabBar ? 0 : 1;
  requestUpdate();
}

void SettingsActivity::navigateButtons() {
  const int ringSize = listCount() + 1;

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    moveRingTo(ButtonNavigator::nextIndex(ringPos(), ringSize));
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    moveRingTo(ButtonNavigator::previousIndex(ringPos(), ringSize));
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    stepTab(1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    stepTab(-1);
    return;
  }
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr) {
  // Theme changes take effect immediately, on this screen — reload the theme
  // and re-derive the app's tokens so the very next repaint is in the new look.
  if (valuePtr != &CrossPointSettings::uiTheme) {
    return;
  }
  UITheme::getInstance().reload();
  // Re-derive the shared tokens for the new look; the gate stays closed until
  // the repaint that rebuilds the interaction table in the new layout.
  resetUi();
}

bool SettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool SettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() > 0) {
      activateIndex(ringPos() - 1);
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    onGoHome();
    return true;
  }

  return false;
}

void SettingsActivity::toggleCurrentSetting() {
  const int visibleIndex = ringPos() - 1;
  if (visibleIndex < 0 || static_cast<size_t>(visibleIndex) >= visibleRows.size()) {
    return;
  }

  const auto& row = visibleRows[static_cast<size_t>(visibleIndex)];
  if (row.isCategory || row.settingIndex >= currentSettings->size()) {
    return;
  }

  const auto& setting = (*currentSettings)[row.settingIndex];
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (setting.enumValues.size() > 2 && !setting.noPopup) {
      const auto valuePtr = setting.valuePtr;
      optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()),
                       currentValue, [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
                         SETTINGS.*valuePtr = idx;
                         SETTINGS.saveToFile();
                         rebuildSettingsLists();
                         applyUiSettingChange(valuePtr);
                       });
      requestUpdate();
      return;
    }
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    if (totalValues > 2 && !setting.noPopup) {
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
        valueSetter(idx);
        SETTINGS.saveToFile();
        rebuildSettingsLists();
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()), cur,
                         std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    setting.valueSetter((cur + 1) % totalValues);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::FileTransfer:
        activityManager.goToFileTransfer();
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;

      // TEMPORARY UI TEST ACTIONS - remove before final firmware.
case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               [this](const ActivityResult&) {
                                 // TextSettingsActivity saves on each change; no save needed here.
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  SETTINGS.saveToFile();
  applyUiSettingChange(setting.valuePtr);
  requestUpdate();
}

void SettingsActivity::openSleepTimeoutPicker() {
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

std::string SettingsActivity::settingValueText(const SettingInfo& setting) {
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

void SettingsActivity::buildScreen(UiScreen& screen) {
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_12_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  // Minuta settings typography: tabs, labels and values all use Steinem 11.
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 6), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  buildTabBar(screen);

  // Give the settings list a comfortable gap below the tab bar.
  // No additional list padding below the tab row.

  // rowItems_ (label/actionValue) was built by rebuildRowItems() when the
  // category was last selected/rebuilt; only the live value text needs
  // refreshing here, by assigning into the existing rowValues_ strings (no
  // vector growth) rather than building a new items/values vector on every
  // render.
  for (size_t i = 0; i < visibleRows.size(); ++i) {
    const auto& row = visibleRows[i];
    if (row.isCategory) {
      rowItems_[i].value = nullptr;
      rowItems_[i].bold = true;
      rowItems_[i].triangleIndicator = true;
      rowItems_[i].triangleDown = sectionExpanded[selectedCategoryIndex][static_cast<size_t>(row.section)];
      continue;
    }

    rowValues_[i] = settingValueText((*currentSettings)[row.settingIndex]);
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.rowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listRowHeight + 11);
  props.rowGap = 0;
  props.valueInset = 8;  // air between the value and the row edge
  // Titles match the value's font size (smallText) so both sides of a row
  // read as one unit; labels that still don't fit wrap onto a second line.
  // maxLines=2 also marks the style explicitly set (an all-default smallText
  // fails textStyleUnset and the list would substitute bodyText back); the
  // common fits-on-one-line case takes the renderer's fast path anyway.
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  props.labelText.lineGap = 6;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void SettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  // Version rides in the header's trailing label slot: the footer position
  // conflicts with button hints on non-touch devices.
  const Rect headerRect{0, metrics.topPadding - 6, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, headerRect, tr(STR_SETTINGS_TITLE), nullptr);

  const int versionWidth = renderer.getTextWidth(UI_10_FONT_ID, CROSSPOINT_VERSION);
  const int versionY = headerRect.y + headerRect.height - renderer.getTextHeight(UI_10_FONT_ID) - 17;
  renderer.drawText(UI_10_FONT_ID, headerRect.x + headerRect.width - metrics.headerSidePadding - versionWidth, versionY,
                    CROSSPOINT_VERSION);

  renderUi();

  if (selectedCategoryIndex == 1) {
    const char* noteLines[5] = {"Hold HOME to clear all caches", "Hold DIRECTION to skip chapter",
                                "Hold READ for recents", "Click POWER to refresh", "Hold POWER to sleep"};
    const int helpLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int textHeight = renderer.getTextHeight(UI_10_FONT_ID);
    const int hintTop = renderer.getScreenHeight() - metrics.buttonHintsHeight;
    const int bottomY = hintTop - 15 - helpLineHeight - 12;
    constexpr int boxPadding = 6;
    for (int i = 0; i < 5; i++) {
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, noteLines[i]);
      const int textX = (renderer.getScreenWidth() - textWidth) / 2;
      const int textY = bottomY - (4 - i) * 36;
      renderer.fillRectDither(textX - boxPadding, textY - boxPadding, textWidth + boxPadding * 2,
                              textHeight + boxPadding * 2, Color::LightGray);
      renderer.drawText(UI_10_FONT_ID, textX, textY, noteLines[i], true, EpdFontFamily::REGULAR);
    }
  }

  const int ring = ringPos();
  const auto confirmLabel = ring == 0 ? "" : tr(STR_TOGGLE);

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), confirmLabel, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
