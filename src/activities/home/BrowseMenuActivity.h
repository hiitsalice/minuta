#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "SettingInfo.h"
#include "activities/UiListActivity.h"

// Menu opened from Solum/Quartum's "Browse" button: one flat list holding the
// library, fonts and device settings. The Controls / Display / Network groups
// are collapsible sections that start collapsed.
class BrowseMenuActivity final : public UiListActivity {
 public:
  explicit BrowseMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  enum class Entry : uint8_t {
    HiddenFeatures,
    Library,
    RecentBooks,
    ReaderFont,
    SdFirmwareUpdate,
    RemapFrontButtons,
    ReaderSideButtons,
    TimeToSleep,
    SleepScreen,
    RefreshFrequency,
    WifiNetworks,
    NetworkMode,
    HotspotMode,
    CalibreWireless,
  };

  struct VisibleRow {
    bool isSection = false;
    int section = 0;  // index into kSections (section rows and their entries)
    Entry entry = Entry::HiddenFeatures;
  };

  static constexpr int SECTION_COUNT = 3;
  static constexpr int MAX_ROWS = 5 + SECTION_COUNT + 9;

  std::vector<SettingInfo> allSettings_;
  std::vector<VisibleRow> visibleRows_;
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;
  bool sectionExpanded_[SECTION_COUNT] = {false, false, false};

  void rebuildVisibleRows();
  void rebuildRowItems();
  const SettingInfo* settingFor(Entry entry) const;
  void cycleSetting(const SettingInfo& setting);
  void openSleepTimeoutPicker();

  bool drawButtonHintDivider() const override { return true; }
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawChrome() override;
};
