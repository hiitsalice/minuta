#pragma once

#include <SdCardFontRegistry.h>

#include <cstdint>
#include <string>
#include <vector>

#include "TextSettingsPreview.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "components/themes/BaseTheme.h"

// Reader text settings: one flat list (Font, Size, then the layout and style rows) with the live
// preview pinned to the bottom of the screen, just above the button hints. Confirm cycles the
// focused row to its next value.
class TextSettingsActivity final : public UiListActivity {
 public:
  // Kept so callers can still say which part of the list to open on.
  enum class Tab : uint8_t { Family, Size, Layout, Style, Count };

  TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const SdCardFontRegistry* registry,
                       Tab initialTab = Tab::Family);

  void onEnter() override;

 private:
  enum class Row : uint8_t {
    Font,
    Size,
    LineSpacing,
    ParaSpacing,
    Alignment,
    ScreenMargin,
    FocusReading,
    Hyphenation,
    EmbeddedStyle,
    AntiAliasing,
    Count
  };
  static constexpr int ROW_COUNT = static_cast<int>(Row::Count);

  enum class LayoutRow { LineSpacing, ParaSpacing, Alignment, ScreenMargin, Count };
  enum class StyleRow { FocusReading, Hyphenation, EmbeddedStyle, AntiAliasing, Count };

  int listCount() const override { return ROW_COUNT; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;
  void drawFooter() override;

  void cycleFont();
  void cycleSize();
  void applyFamily(int listIndex);
  void applySize(int listIndex);
  // Repopulates sizes_ (and currentSizeIndex_) from the active family's installed point sizes.
  void rebuildSizeList();
  void confirmLayoutRow(int row);
  void confirmStyleRow(int row);

  std::string layoutValueText(int row) const;
  std::string styleValueText(int row) const;
  // True when the focused row is a setting the preview cannot reflect.
  bool focusedRowHasNoPreview() const;
  int previewHeightPx() const;
  int previewTopPx() const;

  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;
  };

  struct SizeEntry {
    std::string name;
    uint8_t pointSize;
  };

  const SdCardFontRegistry* registry_;
  std::vector<FontEntry> fonts_;
  std::vector<SizeEntry> sizes_;
  textsettings::PreviewLayout previewLayout_;  // cached preview layout; relaid only on setting/geometry change
  Tab initialTab_;
  int currentFamilyIndex_ = 0;
  int currentSizeIndex_ = 0;

  // Labels/actionValue are set once in onEnter(); values are refreshed in place every buildScreen().
  std::string rowValues_[ROW_COUNT];
  freeink::ui::ListItem rowItems_[ROW_COUNT]{};
};
