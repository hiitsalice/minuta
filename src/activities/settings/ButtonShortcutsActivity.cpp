#include "ButtonShortcutsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"

void ButtonShortcutsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ButtonShortcutsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}

void ButtonShortcutsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();

  // Same header placement as the Settings screen.
  const Rect headerRect{0, metrics.topPadding - 6, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, headerRect, tr(STR_BUTTON_SHORTCUTS), nullptr);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
