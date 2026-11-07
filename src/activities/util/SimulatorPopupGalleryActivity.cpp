#include "SimulatorPopupGalleryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
struct PopupScreen {
  const char* title;
  const char* detail;
};

// Static visual samples only. These deliberately do not construct the real
// activities or touch storage, Wi-Fi, firmware, or reader state.
constexpr PopupScreen kScreens[] = {
    {"Interval selection", "50"},
    {"Go To %", "50%"},
    {"No chapters", ""},
    {"No endnotes on this page", ""},
    {"No saved items", ""},
    {"Clear cache?", "This will clear all cached book data."},
    {"Clearing cache...", ""},
    {"Cache Cleared", "12 item(s) removed"},
    {"Failed to clear cache", "Check serial output for details"},
    {"Loading font list...", ""},
    {"Installing...", ""},
    {"Font installed!", ""},
    {"Font installation failed", "Unable to install font"},
    {"No .bin files found", ""},
    {"No networks found", "Press OK to scan"},
    {"Scanning...", ""},
    {"Connecting...", "Network: Example Wi-Fi"},
    {"Connected!", "Network: Example Wi-Fi  |  IP Address: 192.168.1.100"},
    {"Connection Failed", "Error: Connection timeout"},
    {"Save password for next time?", "Example Wi-Fi"},
    {"Forget Network?", "Example Wi-Fi"},
    {"Checking for update...", ""},
    {"New update available!", "Current Version: 1.4  |  New Version: 1.5"},
    {"Updating...", "45%  |  Do not power off!"},
    {"No update available", ""},
    {"Update failed", "Connection failed"},
    {"Update complete", "Press and hold power button to turn back on"},
    {"Validating firmware...", ""},
    {"Updating...", "45%  |  Do not power off!"},
    {"Update complete", "Restarting..."},
    {"Update failed", "Invalid firmware file"},
    {"Update firmware?", "minuta-v1.5.bin"},
    {"Place .bin on SD card root and select", ""},
    {"Unsupported file format", "Minuta only supports EPUB books"},
    {"Your battery is very low!!!", "Please charge your device and have a rest :)"},
    {"End of book", "Finished"},
    {"SD card error", ""},
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
  const Rect popup = GUI.drawPopup(renderer, screen.title);

  if (screen.detail && screen.detail[0]) {
    const int top = popup.y + popup.height + 22;
    const int availableHeight = renderer.getScreenHeight() - top - 24;
    UITheme::drawCenteredWrappedText(
        renderer, Rect{24, top, renderer.getScreenWidth() - 48, availableHeight},
        UI_10_FONT_ID, screen.detail, 4, true, EpdFontFamily::REGULAR,
        UITheme::TextVerticalAlignment::CENTER, 8);
  }

  renderer.displayBuffer();
}
