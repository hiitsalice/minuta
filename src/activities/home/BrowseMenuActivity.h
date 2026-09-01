#pragma once

#include "activities/UiListActivity.h"

// Simple 2-item menu opened from Solum/Quartum's "Browse" button:
// Library (file browser) and Settings.
class BrowseMenuActivity final : public UiListActivity {
 public:
  explicit BrowseMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEM_COUNT = 2;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawChrome() override;

  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
};
