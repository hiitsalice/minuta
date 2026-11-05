#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class CrashActivity final : public UiListActivity {
 public:
  explicit CrashActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Crash", renderer, mappedInput) {}

  void onEnter() override;

 private:
  int listCount() const override {
    return static_cast<int>(items_.size());
  }

  const char* headerTitle() const override {
    return title_.c_str();
  }

  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void navigateButtons() override;
  void drawFooter() override;
  bool drawButtonHintDivider() const override { return true; }

  void scrollPages(int direction);

  std::string title_;
  std::vector<std::string> rows_;
  std::vector<std::string> boldMasks_;
  std::vector<freeink::ui::ListItem> items_;
  ButtonNavigator navigator_;
};
