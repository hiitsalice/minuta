#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Hidden shortcuts guide: section rows expand/collapse; the reader tool value can be changed,
// and all other rows are informational.
class ButtonShortcutsActivity final : public UiListActivity {
 public:
  explicit ButtonShortcutsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  enum class Level : uint8_t { Section, Trigger, Action };

  struct Row {
    Level level;
    int section;
    std::string text;
    std::string boldTail;
    bool toolSelector = false;
  };

  int listCount() const override { return static_cast<int>(rows_.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawFooter() override;
  bool drawButtonHintDivider() const override { return true; }

  void rebuildRows();

  std::vector<Row> rows_;
  std::vector<freeink::ui::ListItem> items_;
  std::vector<uint8_t> expanded_;
};
