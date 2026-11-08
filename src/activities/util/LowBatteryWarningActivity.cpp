#include "LowBatteryWarningActivity.h"

#include "components/UITheme.h"
#include "fontIds.h"

void LowBatteryWarningActivity::onEnter() {
  Activity::onEnter();

  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, renderer.getScreenWidth(), metrics.headerHeight},
                 "Low Battery Warning");
  renderer.drawCenteredText(UI_10_FONT_ID, 386, "Your battery is very low!!!", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, 414, "Please charge your device and have a rest :)", true,
                            EpdFontFamily::REGULAR);
  // No actions here: keep the (empty) button hint strip visible.
  {
    const auto labels = mappedInput.mapLabels("", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
