#include "UnsupportedFileFormatActivity.h"

#include "components/UITheme.h"
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include "fontIds.h"

void UnsupportedFileFormatActivity::onEnter() {
  Activity::onEnter();
  requestUpdate(true);
}

void UnsupportedFileFormatActivity::loop() {
  // Back is the only active action on this screen.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void UnsupportedFileFormatActivity::render(RenderLock&&) {
  renderer.clearScreen();

  renderer.drawCenteredText(UI_10_FONT_ID, 379, "Unsupported file format", true,
                            EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, 407, "Minuta only supports EPUB books.", true,
                            EpdFontFamily::REGULAR);

  // Show the normal button-hint row, with only Back assigned.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
