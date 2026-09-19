#pragma once

#include <string>

#include "components/UiAppHost.h"

class GfxRenderer;
class MappedInputManager;

class EndOfBookOptions : private UiAppHost {
 public:
  explicit EndOfBookOptions(GfxRenderer& renderer);

  void loadOnce(const std::string& currentBookPath);
  bool menuActive() const;
  void render(GfxRenderer& renderer, const MappedInputManager& input);

 private:
  GfxRenderer& renderer;
};
