#pragma once

#include "activities/Activity.h"

// Guide page listing the hold-button shortcuts. Content to be added.
class ButtonShortcutsActivity final : public Activity {
 public:
  explicit ButtonShortcutsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ButtonShortcuts", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
