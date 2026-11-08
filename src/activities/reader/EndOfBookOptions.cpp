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
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding - 6, safe.width, metrics.headerHeight},
                 "End Of Book");

  UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, 386, "Congratulations!", true, EpdFontFamily::BOLD);
  UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, 414, tr(STR_EOB_FINISHED));

  GUI.drawButtonHints(renderer, "", tr(STR_HOME), "Prev", "");
}
