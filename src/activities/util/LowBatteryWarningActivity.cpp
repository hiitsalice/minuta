#include "LowBatteryWarningActivity.h"

#include "fontIds.h"

void LowBatteryWarningActivity::onEnter() {
  Activity::onEnter();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, 404, "Your battery is very low!!!",
                            true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, 432,
                            "Please charge your device and have a rest :)",
                            true, EpdFontFamily::REGULAR);
  renderer.displayBuffer();
}
