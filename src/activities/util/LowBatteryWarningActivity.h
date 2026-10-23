#pragma once

#include "activities/Activity.h"

class LowBatteryWarningActivity final : public Activity {
 public:
  LowBatteryWarningActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LowBatteryWarning", renderer, mappedInput) {}

  void onEnter() override;
};
