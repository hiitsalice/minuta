#pragma once

#include "activities/Activity.h"

class SimulatorPopupGalleryActivity final : public Activity {
 public:
  SimulatorPopupGalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SimulatorPopupGallery", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  size_t screenIndex = 0;
};
