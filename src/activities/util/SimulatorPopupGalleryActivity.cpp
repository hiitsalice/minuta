#include "SimulatorPopupGalleryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstddef>
#include <cstdint>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
struct PopupLine {
  const char* text;
  int centreY;
  bool bold;
};

enum class ScreenKind : uint8_t { TEXT, SLIDER, PROGRESS };

struct PopupScreen {
  ScreenKind kind;
  PopupLine lines[3];
  int progress;
};

// Static visual samples only. These deliberately do not construct the real
// activities or touch storage, Wi-Fi, firmware, or reader state. Line centres
// follow the fixed positions used by the corresponding Minuta screens.
constexpr PopupScreen kScreens[] = {
    {ScreenKind::SLIDER, {{"30 sec", 360, false}, {"Front buttons: 1 sec", 420, false}, {"Side buttons: 10 sec", 448, false}}, 50},
    {ScreenKind::SLIDER, {{"50%", 360, false}, {"Front buttons: 1%", 420, false}, {"Side buttons: 10%", 448, false}}, 50},
    {ScreenKind::TEXT, {{"No chapters", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"No endnotes on this page", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"No saved items", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Clear cache?", 386, true}, {"This will clear all reading caches", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Clearing cache...", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Cache Cleared", 386, true}, {"12 item(s) removed", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Clear cache failed", 386, true}, {"Check serial output", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Loading font list...", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Installing...", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Font installed!", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Font installation failed", 386, true}, {"Unable to install font", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"No .bin files found", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"No networks found", 386, true}, {"Press RETRY to scan again", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Scanning...", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Connecting...", 386, true}, {"Example Wi-Fi", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Connected!", 372, true}, {"Network: Example Wi-Fi", 400, false}, {"IP Address: 192.168.1.100", 428, false}}, 0},
    {ScreenKind::TEXT, {{"Connection Failed", 386, true}, {"Error: Connection timeout", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Example Wi-Fi", 386, false}, {"Connected! Save password?", 414, true}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Example Wi-Fi", 386, false}, {"Forget and remove password?", 414, true}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Checking for update...", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Update available!", 374, true}, {"Current: 1.4", 402, false}, {"New: 1.5", 426, false}}, 0},
    {ScreenKind::PROGRESS, {{"45%", 380, false}, {"Do not power off!", 425, true}, {nullptr, 0, false}}, 45},
    {ScreenKind::TEXT, {{"No update available", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Update failed", 386, true}, {"Connection failed", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Update complete", 386, true}, {"Press and hold power button to turn back on", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Validating firmware...", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::PROGRESS, {{"45%", 380, false}, {"Do not power off!", 425, true}, {nullptr, 0, false}}, 45},
    {ScreenKind::TEXT, {{"Update complete", 371, true}, {"Restarting... If device does not restart,", 399, false}, {"hold the power button for a few seconds", 423, false}}, 0},
    {ScreenKind::TEXT, {{"Update failed", 386, true}, {"Invalid firmware file", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Update firmware?", 386, true}, {"minuta-v1.5.bin", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Place .bin on SD card root and select", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Unsupported file format", 386, true}, {"Minuta only supports EPUB books", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"Your battery is very low!!!", 386, true}, {"Please charge your device and have a rest :)", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"End of book", 386, true}, {"Finished", 414, false}, {nullptr, 0, false}}, 0},
    {ScreenKind::TEXT, {{"SD card error", 400, false}, {nullptr, 0, false}, {nullptr, 0, false}}, 0},
};

static_assert(sizeof(kScreens) / sizeof(kScreens[0]) == 37, "Popup gallery must contain 37 screens");
}  // namespace

void SimulatorPopupGalleryActivity::onEnter() {
  Activity::onEnter();
  screenIndex = 0;
  requestUpdate();
}

void SimulatorPopupGalleryActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    screenIndex = (screenIndex + 36) % 37;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    screenIndex = (screenIndex + 1) % 37;
    requestUpdate();
  }
}

void SimulatorPopupGalleryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const PopupScreen& screen = kScreens[screenIndex];
  for (const PopupLine& line : screen.lines) {
    if (line.text) {
      renderer.drawCenteredText(UI_10_FONT_ID, line.centreY, line.text, true,
                                line.bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
  }

  if (screen.kind == ScreenKind::SLIDER || screen.kind == ScreenKind::PROGRESS) {
    const int left = 42;
    const int width = renderer.getScreenWidth() - left * 2;
    const int top = screen.kind == ScreenKind::SLIDER ? 389 : 402;
    renderer.drawRect(left, top, width, 10, true);
    renderer.fillRect(left + 1, top + 1, (width - 2) * screen.progress / 100, 8, true);
  }

  renderer.displayBuffer();
}
