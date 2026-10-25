#pragma once

#include <cstdint>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/ButtonNavigator.h"

// Hidden shortcuts guide: a collapsible list like Settings. The bold section rows are the only rows
// that can be selected; the lines under an open section are read-only notes.
class ButtonShortcutsActivity final : public UiListActivity {
 public:
  explicit ButtonShortcutsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  struct Row {
    bool isHeader;
    int section;
    const char* text;
  };

  int listCount() const override { return static_cast<int>(rows_.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void navigateButtons() override;
  void drawFooter() override;

  void rebuildRows();
  void moveToHeader(int direction);

  std::vector<Row> rows_;
  std::vector<freeink::ui::ListItem> items_;
  std::vector<uint8_t> expanded_;
  ButtonNavigator navigator_;
};
