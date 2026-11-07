#include "FullScreenMessageActivity.h"

#include <GfxRenderer.h>

#include "fontIds.h"

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();

  constexpr int top = 400;

  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, top, text.c_str(), true, EpdFontFamily::REGULAR);
  renderer.displayBuffer(refreshMode);
}
