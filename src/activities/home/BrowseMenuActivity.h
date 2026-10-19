#pragma once

#include "activities/UiListActivity.h"

// Menu opened from Solum/Quartum's "Browse" button:
// Settings, Library, Recent Book(s) and Reader Font(s).
class BrowseMenuActivity final : public UiListActivity {
 public:
  explicit BrowseMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEM_COUNT = 4;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawChrome() override;

  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
};
