#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "components/UiAppHost.h"

// Remap front buttons: four role rows (each toggles through the physical buttons)
// plus a reset row. Nothing is saved until Done is pressed with a valid state.
class ButtonRemapActivity final : public Activity, private UiAppHost {
 public:
  explicit ButtonRemapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ButtonRemap", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selectedRow = 0;
  // Temporary mapping: logical role (Back, Confirm, Left, Right) -> physical front
  // button index (0..3), or 0xFF when unassigned. Applied to settings only on Done.
  uint8_t tempMapping[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  freeink::ui::ListItem rowItems[5]{};

  static void screenTrampoline(UiScreen& screen, void* user);
  void buildScreen(UiScreen& screen);
  void cycleRole(uint8_t role);
  void resetAll();
  bool tryApplyAndFinish();
};
