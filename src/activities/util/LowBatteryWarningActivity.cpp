#include "LowBatteryWarningActivity.h"

#include "fontIds.h"

void LowBatteryWarningActivity::onEnter() {
  Activity::onEnter();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, 379, "Your battery is very low!!!",
                            true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, 407,
                            "Please charge your device and have a rest :)",
                            true, EpdFontFamily::REGULAR);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
