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
  constexpr int compositionHeight = 133;
  const int groupTop = (pageHeight - compositionHeight) / 2 + 6;
  const int titleY = groupTop + 3;

  // The bottom of the 'n' in Minuta is titleY + 14.
  // The logo begins at titleY + 20: exactly 5 blank pixels.
  const int logoY = groupTop + 23;

  // The top of the 'p' is statusY + 5.
  // The logo ends at logoY + 85: exactly 9 blank pixels.
  const int statusY = logoY + 89;

  {
    const char* text = "Minuta";
    constexpr int length = 6;
    constexpr int arcDepth = 4;
    constexpr int tracking = 1;
    char glyph[2] = {'\0', '\0'};

    int totalWidth = renderer.getTextAdvanceX(
        UI_12_FONT_ID, text, EpdFontFamily::REGULAR) +
        tracking * (length - 1);
    char prefix[9] = {};
    constexpr int verticalOffsets[6] = {0, -2, -4, -4, -2, 0};
    constexpr int horizontalOffsets[6] = {0, -1, -1, -1, -1, -2};

    int minLeft = 0;
    int maxRight = 0;
    int travelled = 0;

    for (int i = 0; i < length; ++i) {
      glyph[0] = text[i];
      for (int j = 0; j < i; ++j) prefix[j] = text[j];
      prefix[i] = '\0';

      travelled = renderer.getTextAdvanceX(
          UI_12_FONT_ID, prefix, EpdFontFamily::REGULAR) + tracking * i;

      const int advance = renderer.getTextAdvanceX(
          UI_12_FONT_ID, glyph, EpdFontFamily::REGULAR);
      const int left = travelled + horizontalOffsets[i];
      const int right = left + advance;

      if (i == 0 || left < minLeft) minLeft = left;
      if (i == 0 || right > maxRight) maxRight = right;
    }

    const int startX = (pageWidth - (maxRight - minLeft)) / 2 - minLeft;

    for (int i = 0; i < length; ++i) {
      glyph[0] = text[i];
      for (int j = 0; j < i; ++j) prefix[j] = text[j];
      prefix[i] = '\0';
      const int travelled = renderer.getTextAdvanceX(
          UI_12_FONT_ID, prefix, EpdFontFamily::REGULAR) + tracking * i;
      const int advance = renderer.getTextAdvanceX(
          UI_12_FONT_ID, glyph, EpdFontFamily::REGULAR);

      const int offset = verticalOffsets[i];

      renderer.drawText(
          UI_12_FONT_ID, startX + travelled + horizontalOffsets[i],
          titleY + offset - 1,
          glyph, true, EpdFontFamily::REGULAR);

    }
  }

  renderer.drawImage(
      BootingLogo,
      (pageWidth - logoWidth) / 2,
      logoY,
      logoHeight,
      logoWidth);

  const char* text = "booting";
  constexpr int verticalOffsets[] = {-9, -3, 0, 0, 0, -3, -9};
  constexpr int horizontalOffsets[] = {0, 1, 2, 3, 6, 7, 8};
  const int textWidth = renderer.getTextAdvanceX(
      UI_10_FONT_ID, text, EpdFontFamily::REGULAR);
  const int startX = (pageWidth - textWidth - 8) / 2 + 2;

  renderer.drawText(
      UI_10_FONT_ID, startX, statusY + 1, text, true,
      EpdFontFamily::REGULAR, BidiUtils::BidiBaseDir::AUTO,
      verticalOffsets, horizontalOffsets);

  renderer.displayBuffer();
}
