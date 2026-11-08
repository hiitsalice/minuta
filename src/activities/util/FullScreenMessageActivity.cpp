#include "FullScreenMessageActivity.h"

#include <GfxRenderer.h>

#include "components/UITheme.h"
#include "fontIds.h"

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();

  constexpr int top = 400;

  renderer.clearScreen();
  // The only caller is the SD card error screen.
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, renderer.getScreenWidth(), metrics.headerHeight},
                 "SD Card Error");
  renderer.drawCenteredText(UI_10_FONT_ID, top, text.c_str(), true, EpdFontFamily::REGULAR);
  // No actions here: keep the (empty) button hint strip visible.
  {
    const auto labels = mappedInput.mapLabels("", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer(refreshMode);
}
