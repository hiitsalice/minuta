#pragma once

#include "activities/Activity.h"

class UnsupportedFileFormatActivity final : public Activity {
 public:
  explicit UnsupportedFileFormatActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UnsupportedFileFormat", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
