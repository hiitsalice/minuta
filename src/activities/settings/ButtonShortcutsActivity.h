#pragma once

#include <cstdint>
#include <vector>

#include "activities/UiListActivity.h"

// Hidden shortcuts guide: a collapsible list like Settings. Every row can be selected; only the
// bold section rows can be opened or closed, and the rows under them are for reading.
class ButtonShortcutsActivity final : public UiListActivity {
 public:
  explicit ButtonShortcutsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  enum class Level : uint8_t { Section, Trigger, Action };

  struct Row {
    Level level;
    int section;
    const char* text;
    const char* boldTail;
  };

  int listCount() const override { return static_cast<int>(rows_.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawFooter() override;

  void rebuildRows();

  std::vector<Row> rows_;
  std::vector<freeink::ui::ListItem> items_;
  std::vector<uint8_t> expanded_;
};
