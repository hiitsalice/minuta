#include "EndOfBookOptions.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <MappedInputManager.h>

#include "components/UITheme.h"
#include "fontIds.h"

EndOfBookOptions::EndOfBookOptions(GfxRenderer& renderer) : UiAppHost(renderer), renderer(renderer) {}

void EndOfBookOptions::loadOnce(const std::string&) {}

bool EndOfBookOptions::menuActive() const { return false; }

void EndOfBookOptions::render(GfxRenderer& renderer, const MappedInputManager& input) {
  const auto& safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, 376, tr(STR_END_OF_BOOK), true,
                            EpdFontFamily::BOLD);
  UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, 404, tr(STR_EOB_FINISHED));

  GUI.drawButtonHints(renderer, "", tr(STR_HOME), "Prev", "");
}
