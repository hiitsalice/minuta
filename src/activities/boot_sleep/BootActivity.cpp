#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Centre the logo + Minuta + status text as one vertical group.
  constexpr int logoSize = 120;
  constexpr int logoTextGap = -30;
  constexpr int textLineGap = 8;

  const int titleHeight = renderer.getTextHeight(UI_18_FONT_ID);
  const int statusHeight = renderer.getTextHeight(UI_10_FONT_ID);

  const int groupHeight =
      logoSize + logoTextGap + titleHeight + textLineGap + statusHeight;
  const int groupTop = (pageHeight - groupHeight) / 2;

  const int logoY = groupTop;
  const int titleY = logoY + logoSize + logoTextGap;
  const int statusY = titleY + titleHeight + textLineGap;

  renderer.drawImage(
      Logo120,
      (pageWidth - logoSize) / 2,
      logoY,
      logoSize,
      logoSize);

  renderer.drawCenteredText(
      UI_18_FONT_ID,
      titleY,
      "Minuta",
      true,
      EpdFontFamily::REGULAR);

  renderer.drawCenteredText(
      UI_10_FONT_ID,
      statusY,
      tr(STR_BOOTING),
      true,
      EpdFontFamily::ITALIC);

  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
