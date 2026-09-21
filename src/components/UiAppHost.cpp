#include "UiAppHost.h"

#include "UiAppHelpers.h"

namespace fui = freeink::ui;

UiAppHost::UiAppHost(const GfxRenderer& renderer)
    : uiTarget(makeUiTarget(renderer)), app(uiTarget, uiTarget.deviceContext()) {}

void UiAppHost::resetUi() {
  uiReady = false;
  applySharedUiTheme(app, uiTarget);
}

void UiAppHost::renderUi() {
  app.setDevice(uiTarget.deviceContext());
  app.render();
  uiReady = true;
}

fui::ActionEvent UiAppHost::route(const fui::InputSnapshot& snap) {
  if (!uiReady) return {};
  return app.route(snap);
}
