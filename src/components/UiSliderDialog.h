#pragma once
#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/UiAppHost.h"

// Shared slider-dialog screen: a centered value readout above a drag slider
// with -/+ fine-step tap zones at the row ends, a Cancel/OK pair on touch
// boards, and two step-hint lines on button boards. Used by the interval and
// percent selection dialogs, which differ only in how they format the readout
// and hints and what the actions do.
struct UiSliderDialogSpec {
  const char* readout = nullptr;  // preformatted current-value text
  int value = 0;                  // slider position (0-based within max)
  int max = 1;                    // slider range
  freeink::ui::ActionId sliderAction = 0;
  freeink::ui::ActionId stepAction = 0;  // dispatched with value -1 / +1
  freeink::ui::ActionId cancelAction = 0;
  freeink::ui::ActionId okAction = 0;
  // Step hints for button boards (small step, large step); skipped on touch.
  const char* hintLine1 = nullptr;
  const char* hintLine2 = nullptr;
  int16_t sliderY = -1;
  int16_t readoutY = -1;
  int readoutFontId = -1;
  int16_t hintLine1Y = -1;
  int16_t hintLine2Y = -1;
};

inline void buildSliderDialogScreen(UiAppHost::UiScreen& screen, const GfxRenderer& renderer,
                                    const MappedInputManager& mappedInput, const UiSliderDialogSpec& spec) {
  namespace fui = freeink::ui;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the title band render() paints, dropped down
  // to where the legacy fixed layout placed the readout.
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 4),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  // Value readout, centered at the requested Y position.
  fui::TextStyle readout = theme.smallText;
  if (spec.readoutFontId >= 0) {
    readout.font = spec.readoutFontId;
  }
  readout.align = fui::TextAlign::Center;
  readout.bold = true;
  const int16_t readoutLh = screen.target().lineHeight(readout.font);
  const int16_t readoutY = spec.readoutY >= 0
      ? spec.readoutY
      : screen.takeTop(readoutLh, theme.spaceLg).y;
  screen.target().text(fui::Rect{safe.x, static_cast<int16_t>(readoutY - readoutLh / 2),
                                 safe.width, readoutLh},
                       spec.readout, readout);

  // Slider row: -/+ tap zones at the row ends for fine steps (a full-row-height
  // square each, comfortable touch targets), with the drag slider between them.
  // A small gap keeps the slider's (min-touch-expanded) hit rect from overlapping.
  const fui::Insets sideInset{0, static_cast<int16_t>(theme.spaceLg * 2), 0, static_cast<int16_t>(theme.spaceLg * 2)};
  const fui::Rect row = spec.sliderY >= 0
      ? fui::Rect{safe.x, static_cast<int16_t>(spec.sliderY - theme.rowHeight / 2),
                   safe.width, theme.rowHeight}.inset(sideInset)
      : screen.takeTop(theme.rowHeight, theme.spaceLg).inset(sideInset);
  const int16_t stepW = row.height;
  const fui::Rect minusHit{row.x, row.y, stepW, row.height};
  const fui::Rect plusHit{static_cast<int16_t>(row.right() - stepW), row.y, stepW, row.height};

  fui::TextStyle glyph = theme.bodyText;
  glyph.align = fui::TextAlign::Center;
  const int16_t glyphLh = screen.target().lineHeight(glyph.font);
  const int16_t glyphY = static_cast<int16_t>(row.y + (row.height - glyphLh) / 2);
  const int16_t minusGlyphY = static_cast<int16_t>(glyphY - 1);
  const int16_t plusGlyphY = static_cast<int16_t>(glyphY + 1);
  constexpr int16_t glyphInset = 14;
  screen.target().text(
      fui::Rect{static_cast<int16_t>(minusHit.x + glyphInset), minusGlyphY, stepW, glyphLh}, "-", glyph);
  screen.target().text(
      fui::Rect{static_cast<int16_t>(plusHit.x - glyphInset), plusGlyphY, stepW, glyphLh}, "+", glyph);

  fui::SliderProps props;
  props.value = spec.value;
  props.max = spec.max;
  props.action = spec.sliderAction;
  constexpr int16_t sideGap = 56;
  fui::slider(screen.frame(), row.inset(fui::Insets{0, sideGap, 0, sideGap}), props);

  // Two-line step hint (front buttons = fine step, side buttons = coarse step),
  // preformatted by the caller so the layout doesn't depend on a separator
  // hidden in translated text.
  fui::TextStyle hint = theme.smallText;
  hint.align = fui::TextAlign::Center;
  const int16_t hintLh = screen.target().lineHeight(hint.font);
  if (spec.hintLine1) {
    const fui::Rect hintRect = spec.hintLine1Y >= 0
        ? fui::Rect{safe.x, spec.hintLine1Y, safe.width, hintLh}
        : screen.takeTop(hintLh, theme.spaceSm);
    screen.target().text(hintRect, spec.hintLine1, hint);
  }
  if (spec.hintLine2) {
    const fui::Rect hintRect = spec.hintLine2Y >= 0
        ? fui::Rect{safe.x, spec.hintLine2Y, safe.width, hintLh}
        : screen.takeTop(hintLh);
    screen.target().text(hintRect, spec.hintLine2, hint);
  }
}
