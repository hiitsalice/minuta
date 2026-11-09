#pragma once
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <FreeInkUIIcon.h>
#include <I18n.h>

#include <atomic>
#include <string>

#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/icons/customListIcons.h"
#include "components/icons/listIcons.h"

// Shared glue for activities hosting a FreeInkApp: the font-bound render
// target and the touch snapshot FreeInkApp routing consumes.

// One app-wide ThemeTokens instance shared by every FreeInkApp via
// setThemeRef, so per-app copies (~1.5KB each, and one per stacked activity)
// aren't pure heap waste. Refreshed on every screen entry, so theme or font
// changes between activities re-derive it; live theme changes (Settings)
// refresh it and every referencing app repaints in the new look.
//
// Backed by a small pool + an atomic cell (FreeInkApp::setThemeRef() takes a
// pointer to the cell, not to a ThemeTokens instance directly) rather than a
// single instance overwritten in place: refreshSharedUiThemeTokens() below
// always builds the new tokens into whichever pool slot the cell does NOT
// currently reference, then does one atomic store. Every app sharing the
// cell picks up the change on its next theme() call, and nothing ever
// dereferences an instance mid-overwrite — unlike a plain
// `sharedTokens = uiThemeTokens(target);` in-place assignment, which a
// render task reading theme().rowHeight/etc. field-by-field on another task
// could observe as a torn mix of old and new fields.
inline std::atomic<const freeink::ui::ThemeTokens*>& sharedUiThemeCell() {
  static std::atomic<const freeink::ui::ThemeTokens*> cell{nullptr};
  return cell;
}

// Rebuilds the shared tokens for `target` and atomically publishes them via
// sharedUiThemeCell(). Returns the freshly-published instance for callers
// that also want to read it back immediately (e.g. BaseTheme::drawHeader(),
// which derives the same tokens as a render-path scratch value instead of
// stack-allocating its own copy).
inline const freeink::ui::ThemeTokens& refreshSharedUiThemeTokens(const freeink::ui::GfxRendererTarget& target) {
  static freeink::ui::ThemeTokens pool[2];
  auto& cell = sharedUiThemeCell();
  const auto* current = cell.load(std::memory_order_relaxed);
  freeink::ui::ThemeTokens* next = (current == &pool[0]) ? &pool[1] : &pool[0];
  *next = uiThemeTokens(target);
  cell.store(next, std::memory_order_release);
  return *next;
}

// Refresh the shared tokens from the active UITheme + this target's fonts and
// point the app at them. Replaces the old per-app `app.setTheme(...)` copies.
template <typename App>
inline void applySharedUiTheme(App& app, const freeink::ui::GfxRendererTarget& target) {
  refreshSharedUiThemeTokens(target);
  app.setThemeRef(&sharedUiThemeCell());
}

// Bind the uiScale fonts before FreeInkApp's constructor derives its theme
// metrics from the body font's line height.
inline freeink::ui::GfxRendererTarget makeUiTarget(const GfxRenderer& renderer) {
  freeink::ui::GfxRendererTarget target(renderer);
  const auto spec = uiScaleSpec();
  target.setFont(freeink::ui::GfxRendererTarget::FONT_SMALL, spec.smallFontId);
  target.setFont(freeink::ui::GfxRendererTarget::FONT_BODY, spec.bodyFontId);
  target.setFont(freeink::ui::GfxRendererTarget::FONT_TITLE, spec.titleFontId);
  return target;
}

// Tap release with coords, plus the raw release the tap classifier never
// reports (swipe end, drag-off) delivered off-target: nothing dispatches,
// but routing drops its pressed-element state instead of ghosting it onto
// the next render.
// Firmware UIIcon -> FreeInkUI bitmap for list rows (SDK-format icons only;
// the legacy drawIcon assets use a different bit layout). Two crisp sizes:
// 24 for single-line rows, 32 for label+subtitle rows.
inline freeink::ui::BitmapRef listIconFor(const UIIcon icon, const int size = 24) {
  if (size >= 32) {
    switch (icon) {
      case UIIcon::Folder:
        return freeink::ui::bitmapFromIcon(icon_folder_32);
      case UIIcon::Text:
        return freeink::ui::bitmapFromIcon(icon_file_text_32);
      case UIIcon::Image:
        return freeink::ui::bitmapFromIcon(icon_image_32);
      case UIIcon::Book:
        return freeink::ui::bitmapFromIcon(icon_book_32);
      case UIIcon::File:
        return freeink::ui::bitmapFromIcon(icon_file_32);
      case UIIcon::Wifi:
        return freeink::ui::bitmapFromIcon(icon_wifi_32);
      case UIIcon::Library:
        return freeink::ui::bitmapFromIcon(icon_library_32);
      case UIIcon::Hotspot:
        return freeink::ui::bitmapFromIcon(icon_radio_tower_32);
      case UIIcon::Usb:
        return freeink::ui::bitmapFromIcon(icon_usb_32);
      case UIIcon::Bookmark:
        return freeink::ui::bitmapFromIcon(icon_bookmark_32);
      default:
        return {};
    }
  }
  switch (icon) {
    case UIIcon::Folder:
      return freeink::ui::bitmapFromIcon(icon_folder_24);
    case UIIcon::Text:
      return freeink::ui::bitmapFromIcon(icon_file_text_24);
    case UIIcon::Image:
      return freeink::ui::bitmapFromIcon(icon_image_24);
    case UIIcon::Book:
      return freeink::ui::bitmapFromIcon(icon_book_24);
    case UIIcon::File:
      return freeink::ui::bitmapFromIcon(icon_file_24);
    case UIIcon::Wifi:
      return freeink::ui::bitmapFromIcon(icon_wifi_24);
    case UIIcon::Library:
      return freeink::ui::bitmapFromIcon(icon_library_24);
    case UIIcon::Hotspot:
      return freeink::ui::bitmapFromIcon(icon_radio_tower_24);
    case UIIcon::Usb:
      return freeink::ui::bitmapFromIcon(icon_usb_24);
    case UIIcon::Bookmark:
      return freeink::ui::bitmapFromIcon(icon_bookmark_24);
    default:
      return {};
  }
}



// Creates one mask glyph per UTF-8 character, rather than one per byte.
inline std::string maskListPassword(const std::string& password) {
  size_t characterCount = 0;
  for (const unsigned char byte : password) {
    if ((byte & 0xC0) != 0x80) ++characterCount;
  }
  return std::string(characterCount, '*');
}

// Fits a trailing list value into the area after its label. FreeInkUI draws
// values right-aligned; this helper prepares a display-only string so a long
// value cannot consume the label's space. Truncation removes complete UTF-8
// code points from the start and keeps the end of the original value.
template <typename Screen>
inline std::string fitListValueAfterLabel(const freeink::ui::GfxRendererTarget& target, const Screen& screen,
                                          const freeink::ui::ListProps& props, const char* label,
                                          const std::string& value, const int16_t minimumGap = 36) {
  namespace fui = freeink::ui;
  if (value.empty() || !label) return value;

  const auto& theme = screen.theme();
  const fui::Rect body = screen.body();
  const int16_t rowHeight = props.rowHeight > 0 ? props.rowHeight : theme.rowHeight;
  const int16_t rowGap = props.rowGap >= 0 ? props.rowGap : theme.listRowGap;
  const uint16_t visibleRows = fui::listVisibleRows(body, rowHeight, rowGap);

  const int16_t rowInset = props.rowInset >= 0 ? props.rowInset : theme.listInset;
  int32_t rowWidth = body.width - static_cast<int32_t>(rowInset) * 2;
  const int16_t scrollWidth = props.scrollIndicatorWidth >= 0 ? props.scrollIndicatorWidth : theme.listScrollWidth;
  const int16_t scrollInset = props.scrollIndicatorInset >= 0 ? props.scrollIndicatorInset : theme.listScrollInset;
  if (props.scrollIndicator && props.count > visibleRows && scrollWidth > 0) {
    const int16_t needed = static_cast<int16_t>(scrollWidth + scrollInset + 2);
    if (rowInset < needed) rowWidth -= needed - rowInset;
  }

  const int16_t sidePadding = props.sidePadding >= 0 ? props.sidePadding : theme.listSidePadding;
  const int32_t bandWidth = rowWidth - static_cast<int32_t>(sidePadding) * 2;
  const int16_t labelWidth = target.measureText(props.labelText.font, label, props.labelText).width;
  freeink::ui::TextStyle valueStyle = props.valueText;
  valueStyle.align = freeink::ui::TextAlign::Right;

  const int16_t labelOffset = props.labelXOffset;
  const int16_t requiredGap = minimumGap > props.textGap + labelOffset
                                  ? minimumGap
                                  : static_cast<int16_t>(props.textGap + labelOffset);
  const int32_t maxValueWidth = bandWidth - props.valueInset - labelWidth - requiredGap;
  if (maxValueWidth <= 0) return "";

  if (target.measureText(valueStyle.font, value.c_str(), valueStyle).width <= maxValueWidth) return value;

  constexpr const char* ELLIPSIS = "...";
  if (target.measureText(valueStyle.font, ELLIPSIS, valueStyle).width > maxValueWidth) return "";

  // Keep the beginning of the value and remove complete UTF-8 code points
  // from the end until the retained prefix plus ellipsis fits.
  size_t end = value.size();
  while (end > 0) {
    std::string candidate(value, 0, end);
    candidate.append(ELLIPSIS);
    if (target.measureText(valueStyle.font, candidate.c_str(), valueStyle).width <= maxValueWidth) return candidate;

    // Move back over continuation bytes, then remove the leading byte of
    // the final UTF-8 code point on the next iteration.
    --end;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) --end;
  }

  return ELLIPSIS;
}
