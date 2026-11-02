#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/BootingLogo.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  constexpr int logoWidth = 150;
  constexpr int logoHeight = 86;
  constexpr int arcDepth = 8;
  constexpr int tracking = 1;

  // Centre the actual curved glyph bounds, not the font line boxes.
  // The title's outermost glyph pixels extend from titleY - 3.
  constexpr int compositionHeight = 128;
  const int groupTop = (pageHeight - compositionHeight) / 2;
  const int titleY = groupTop + 3;

  // The bottom of the 'n' in Minuta is titleY + 14.
  // The logo begins at titleY + 21: exactly 6 blank pixels.
  const int logoY = titleY + 21;

  // The top of the 'p' is statusY + 5.
  // The logo ends at logoY + 85: exactly 9 blank pixels.
  const int statusY = logoY + 89;

  {
    const char* text = "Minuta";
    constexpr int length = 6;
    constexpr int arcDepth = 4;
    constexpr int tracking = 1;
    char glyph[2] = {'\0', '\0'};

    int totalWidth = tracking * (length - 1);
    for (int i = 0; i < length; ++i) {
      glyph[0] = text[i];
      totalWidth += renderer.getTextAdvanceX(
          UI_12_FONT_ID, glyph, EpdFontFamily::REGULAR);
    }

    int x = (pageWidth - totalWidth) / 2;
    int travelled = 0;

    for (int i = 0; i < length; ++i) {
      glyph[0] = text[i];
      const int advance = renderer.getTextAdvanceX(
          UI_12_FONT_ID, glyph, EpdFontFamily::REGULAR);

      const int delta = travelled * 2 + advance - totalWidth;
      const int offset = totalWidth > 0
          ? -arcDepth * (totalWidth * totalWidth - delta * delta) /
                (totalWidth * totalWidth)
          : 0;

      renderer.drawText(
          UI_12_FONT_ID, x, titleY + offset,
          glyph, true, EpdFontFamily::REGULAR);

      x += advance + tracking;
      travelled += advance + tracking;
    }
  }

  renderer.drawImage(
      BootingLogo,
      (pageWidth - logoWidth) / 2,
      logoY,
      logoHeight,
      logoWidth);

  const char* text = "booting";
  const int length = 7;
  char glyph[2] = {'\0', '\0'};

  int totalWidth = tracking * (length - 1);
  for (int i = 0; i < length; ++i) {
    glyph[0] = text[i];
    totalWidth += renderer.getTextAdvanceX(
        UI_10_FONT_ID, glyph, EpdFontFamily::REGULAR);
  }

  int x = (pageWidth - totalWidth) / 2;
  int travelled = 0;

  for (int i = 0; i < length; ++i) {
    glyph[0] = text[i];
    const int advance = renderer.getTextAdvanceX(
        UI_10_FONT_ID, glyph, EpdFontFamily::REGULAR);

    const int delta = travelled * 2 + advance - totalWidth;
    const int offset = totalWidth > 0
        ? -arcDepth * delta * delta / (totalWidth * totalWidth)
        : 0;

    renderer.drawText(
        UI_10_FONT_ID, x, statusY + offset,
        glyph, true, EpdFontFamily::REGULAR);

    x += advance + tracking;
    travelled += advance + tracking;
  }

  renderer.displayBuffer();
}
