#include "KeyboardEntryActivity.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

constexpr fui::ActionId ACTION_KEY = 1;

// ---------------------------------------------------------------------------
// URL layers. The SDK builtin layouts have no URL variant (":", "/", ".", the
// snippet panel), so these are app-defined tables over the same public
// KeyboardLayout structs. URLs are ASCII, so the letter rows are EN-arranged
// regardless of UI language.
// ---------------------------------------------------------------------------

#define UK(label, output, value) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, 1, true, nullptr }
#define UKA(label, output, value, alt) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, 1, true, alt }
#define UKW(label, output, value, units) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, units, true, nullptr }
#define UKS(label, kind, value, units) \
  fui::KeyboardKey { label, nullptr, kind, fui::StateNormal, value, units, true, nullptr }

constexpr int16_t URL_PANEL_VALUE = -3;  // mirrors KeyboardEntryActivity::URL_PANEL_KEY

const fui::KeyboardKey URL_NUM_ROW[] = {UKA("1", "1", '1', "!"), UKA("2", "2", '2', "@"), UKA("3", "3", '3', "#"),
                                        UKA("4", "4", '4', "$"), UKA("5", "5", '5', "%"), UKA("6", "6", '6', "^"),
                                        UKA("7", "7", '7', "&"), UKA("8", "8", '8', "*"), UKA("9", "9", '9', "("),
                                        UKA("0", "0", '0', ")")};

const fui::KeyboardKey URL_ROW1[] = {UK("q", "q", 'q'), UK("w", "w", 'w'), UK("e", "e", 'e'), UK("r", "r", 'r'),
                                     UK("t", "t", 't'), UK("y", "y", 'y'), UK("u", "u", 'u'), UK("i", "i", 'i'),
                                     UK("o", "o", 'o'), UK("p", "p", 'p')};
const fui::KeyboardKey URL_ROW2[] = {UK("a", "a", 'a'), UK("s", "s", 's'), UK("d", "d", 'd'),
                                     UK("f", "f", 'f'), UK("g", "g", 'g'), UK("h", "h", 'h'),
                                     UK("j", "j", 'j'), UK("k", "k", 'k'), UK("l", "l", 'l')};
const fui::KeyboardKey URL_ROW3[] = {UKS("Shift", fui::KeyKind::Shift, fui::QWERTY_KEY_SHIFT, 2),
                                     UK("z", "z", 'z'),
                                     UK("x", "x", 'x'),
                                     UK("c", "c", 'c'),
                                     UK("v", "v", 'v'),
                                     UK("b", "b", 'b'),
                                     UK("n", "n", 'n'),
                                     UK("m", "m", 'm'),
                                     UKS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
// URLs have no spaces, so the URL bottom row spends the space slot on ":",
// "/", "." and the snippet-panel toggle instead (the legacy keyboard did the
// same with its "URL" key).
const fui::KeyboardKey URL_BOTTOM[] = {UKS("?123", fui::KeyKind::Mode, fui::QWERTY_KEY_MODE, 2),
                                       UK(":", ":", ':'),
                                       UK("/", "/", '/'),
                                       UK(".", ".", '.'),
                                       UKW("URL", nullptr, URL_PANEL_VALUE, 2),
                                       UKS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 2)};

const fui::KeyboardKey URL_SHIFT_ROW1[] = {UK("Q", "Q", 'Q'), UK("W", "W", 'W'), UK("E", "E", 'E'), UK("R", "R", 'R'),
                                           UK("T", "T", 'T'), UK("Y", "Y", 'Y'), UK("U", "U", 'U'), UK("I", "I", 'I'),
                                           UK("O", "O", 'O'), UK("P", "P", 'P')};
const fui::KeyboardKey URL_SHIFT_ROW2[] = {UK("A", "A", 'A'), UK("S", "S", 'S'), UK("D", "D", 'D'),
                                           UK("F", "F", 'F'), UK("G", "G", 'G'), UK("H", "H", 'H'),
                                           UK("J", "J", 'J'), UK("K", "K", 'K'), UK("L", "L", 'L')};
const fui::KeyboardKey URL_SHIFT_ROW3[] = {UKS("Shift", fui::KeyKind::Shift, fui::QWERTY_KEY_SHIFT, 2),
                                           UK("Z", "Z", 'Z'),
                                           UK("X", "X", 'X'),
                                           UK("C", "C", 'C'),
                                           UK("V", "V", 'V'),
                                           UK("B", "B", 'B'),
                                           UK("N", "N", 'N'),
                                           UK("M", "M", 'M'),
                                           UKS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};

// Snippet keys: multi-character outputs, stable ids above the localized-key
// range so they never collide with layout key ids.
const fui::KeyboardKey URL_SNIP_ROW1[] = {UK("https://", "https://", 2001), UK("www.", "www.", 2002),
                                          UK(".com", ".com", 2003)};
const fui::KeyboardKey URL_SNIP_ROW2[] = {UK("http://", "http://", 2004), UK("192.168.", "192.168.", 2005),
                                          UK(".org", ".org", 2006)};
const fui::KeyboardKey URL_SNIP_ROW3[] = {UK("/opds", "/opds", 2007), UK(":8080", ":8080", 2008),
                                          UK(".net", ".net", 2009)};
const fui::KeyboardKey URL_SNIP_BOTTOM[] = {UKS("abc", fui::KeyKind::Mode, fui::QWERTY_KEY_MODE, 2),
                                            UKS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2),
                                            UKS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 2)};

#undef UK
#undef UKA
#undef UKW
#undef UKS

const fui::KeyboardRow URL_ROWS[] = {
    {URL_NUM_ROW, 10, 0}, {URL_ROW1, 10, 0}, {URL_ROW2, 9, 1}, {URL_ROW3, 9, 0}, {URL_BOTTOM, 6, 0}};
const fui::KeyboardRow URL_SHIFT_ROWS[] = {
    {URL_NUM_ROW, 10, 0}, {URL_SHIFT_ROW1, 10, 0}, {URL_SHIFT_ROW2, 9, 1}, {URL_SHIFT_ROW3, 9, 0}, {URL_BOTTOM, 6, 0}};
const fui::KeyboardRow URL_SNIP_ROWS[] = {
    {URL_SNIP_ROW1, 3, 0}, {URL_SNIP_ROW2, 3, 0}, {URL_SNIP_ROW3, 3, 0}, {URL_SNIP_BOTTOM, 3, 0}};

const fui::KeyboardLayout URL_LAYOUT{URL_ROWS, 5};
const fui::KeyboardLayout URL_SHIFT_LAYOUT{URL_SHIFT_ROWS, 5};
const fui::KeyboardLayout URL_SNIPPET_LAYOUT{URL_SNIP_ROWS, 4};

fui::KeyboardLayoutId layoutForLanguage(const Language) { return fui::KeyboardLayoutId::QwertyEn; }

}  // namespace

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  cursorPos = text.length();
  // URL layers are EN-arranged app tables; everything else follows the UI
  // language.
  layoutId = inputType == InputType::Url ? fui::KeyboardLayoutId::QwertyEn : layoutForLanguage(I18N.getLanguage());
  shifted = false;
  symbols = false;
  urlPanel = false;
  cursorMode = false;
  togglePos = false;
  passwordVisible = false;
  selRow = 0;
  selCol = 0;
  rightHeld = false;
  rightLongHandled = false;
  savedCursorPos = 0;
  rightStartCursorPos = 0;
  upHeld = false;
  upLongHandled = false;
  downHeld = false;
  downLongHandled = false;
  confirmHeld = false;
  confirmLongHandled = false;
  interactionsReady = false;
  requestUpdate();
}

void KeyboardEntryActivity::onExit() { Activity::onExit(); }

const fui::KeyboardLayout& KeyboardEntryActivity::currentLayout() const {
  if (symbols) return fui::builtinKeyboardLayout(layoutId, shifted, true);
  if (inputType == InputType::Url) {
    if (urlPanel) return URL_SNIPPET_LAYOUT;
    return shifted ? URL_SHIFT_LAYOUT : URL_LAYOUT;
  }
  return fui::builtinKeyboardLayout(layoutId, shifted, false, /*numberRow=*/true);
}

const fui::KeyboardKey* KeyboardEntryActivity::selectedKey() const {
  const fui::KeyboardLayout& layout = currentLayout();
  if (selRow < 0 || selRow >= layout.rowCount) return nullptr;
  const fui::KeyboardRow& row = layout.rows[selRow];
  if (selCol < 0 || selCol >= row.count) return nullptr;
  return &row.keys[selCol];
}

int KeyboardEntryActivity::selectedLogicalIndex() const {
  const fui::KeyboardLayout& layout = currentLayout();
  int index = 0;
  for (int r = 0; r < selRow && r < layout.rowCount; r++) {
    index += layout.rows[r].count;
  }
  return index + selCol;
}

void KeyboardEntryActivity::clampSelection() {
  const fui::KeyboardLayout& layout = currentLayout();
  if (layout.rowCount == 0) {
    selRow = 0;
    selCol = 0;
    return;
  }
  if (selRow < 0) selRow = 0;
  if (selRow >= layout.rowCount) selRow = layout.rowCount - 1;
  const int cols = layout.rows[selRow].count;
  if (selCol < 0) selCol = 0;
  if (selCol >= cols) selCol = cols > 0 ? cols - 1 : 0;
}

void KeyboardEntryActivity::moveSelectionRow(const int delta) {
  const fui::KeyboardLayout& layout = currentLayout();
  if (layout.rowCount == 0) return;
  const int oldCols = selRow < layout.rowCount ? layout.rows[selRow].count : 1;
  selRow = (selRow + delta + layout.rowCount) % layout.rowCount;
  const int newCols = layout.rows[selRow].count;
  // Proportional column mapping keeps vertical travel intuitive between rows
  // of different key counts (e.g. a 10-key letter row over a 6-key bottom row).
  if (oldCols > 0 && newCols > 0 && oldCols != newCols) {
    selCol = selCol * newCols / oldCols;
  }
  clampSelection();
}

void KeyboardEntryActivity::moveSelectionCol(const int delta) {
  const fui::KeyboardLayout& layout = currentLayout();
  if (selRow < 0 || selRow >= layout.rowCount) return;
  const int cols = layout.rows[selRow].count;
  if (cols <= 0) return;
  selCol = (selCol + delta + cols) % cols;
}

bool KeyboardEntryActivity::syncSelectionToValue(const int16_t value) {
  const fui::KeyboardLayout& layout = currentLayout();
  for (int r = 0; r < layout.rowCount; r++) {
    for (int c = 0; c < layout.rows[r].count; c++) {
      if (layout.rows[r].keys[c].value == value) {
        selRow = r;
        selCol = c;
        return true;
      }
    }
  }
  return false;
}

size_t KeyboardEntryActivity::utf8Prev(const std::string& s, size_t pos) {
  if (pos == 0) return 0;
  pos--;
  while (pos > 0 && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) pos--;
  return pos;
}

size_t KeyboardEntryActivity::utf8Next(const std::string& s, size_t pos) {
  if (pos >= s.length()) return s.length();
  pos++;
  while (pos < s.length() && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) pos++;
  return pos;
}

void KeyboardEntryActivity::insertUtf8(const char* out) {
  if (!out || !*out) return;
  const size_t n = strlen(out);
  if (maxLength != 0 && text.length() + n > maxLength) return;
  if (cursorPos > text.length()) cursorPos = text.length();
  text.insert(cursorPos, out, n);
  cursorPos += n;
}

bool KeyboardEntryActivity::backspaceUtf8() {
  if (text.empty() || cursorPos == 0) return false;
  const size_t prev = utf8Prev(text, cursorPos);
  text.erase(prev, cursorPos - prev);
  cursorPos = prev;
  return true;
}

bool KeyboardEntryActivity::activateValue(const int16_t value, const bool longPress) {
  switch (value) {
    case fui::QWERTY_KEY_SHIFT:
      // Letters: case toggle. Symbols: pages between "?123" and "#+=".
      shifted = !shifted;
      clampSelection();
      return true;
    case fui::QWERTY_KEY_MODE:
      if (urlPanel) {
        urlPanel = false;
      } else {
        symbols = !symbols;
        shifted = false;
      }
      clampSelection();
      return true;
    case URL_PANEL_KEY:
      urlPanel = !urlPanel;
      symbols = false;
      shifted = false;
      clampSelection();
      return true;
    case fui::QWERTY_KEY_ENTER:
      onComplete(text);
      return false;
    case fui::QWERTY_KEY_BACKSPACE:
      if (longPress) {
        text.clear();
        cursorPos = 0;
        return true;
      }
      backspaceUtf8();
      return true;
    default: {
      const fui::KeyboardLayout& layer = currentLayout();
      // keyboardAltOutputFor covers explicit alts and the letter case-flip.
      const char* out = longPress ? fui::keyboardAltOutputFor(layer, value) : nullptr;
      if (!out) out = fui::keyboardOutputFor(layer, value);
      if (!out) return false;
      insertUtf8(out);
      if (shifted && !symbols) {
        shifted = false;  // shift auto-releases after one character
        clampSelection();
      }
      return true;
    }
  }
}

bool KeyboardEntryActivity::clearAllOrAltOnSelected() {
  const fui::KeyboardKey* key = selectedKey();
  if (!key) return false;
  if (key->value == fui::QWERTY_KEY_BACKSPACE) {
    text.clear();
    cursorPos = 0;
    return true;
  }
  // Explicit alts and the letter case-flip, same as touch long-press.
  const char* alt = fui::keyboardAltOutputFor(currentLayout(), key->value);
  if (alt) {
    insertUtf8(alt);
    return true;
  }
  return false;
}

std::string KeyboardEntryActivity::displayTextForCurrentState() const {
  std::string displayText = text;
  if (inputType != InputType::Password || passwordVisible) {
    return displayText;
  }

  size_t revealPos;
  if (cursorMode) {
    revealPos = text.length();  // no reveal in displayText; block draws actual char directly
  } else {
    revealPos = (text.length() > 0 && cursorPos > 0) ? cursorPos - 1 : std::string::npos;
  }
  for (size_t i = 0; i < displayText.length(); i++) {
    if (i != revealPos) {
      displayText[i] = '*';
    }
  }
  return displayText;
}

int KeyboardEntryActivity::measureRange(std::string& s, const int start, const int end) const {
  if (end <= start) return 0;
  // s[end] is writable even at s.length() (the terminator slot); only '\0' may
  // be written there, which is exactly what the measurement needs.
  const char saved = s[end];
  s[end] = '\0';
  const int width = renderer.getTextAdvanceX(UI_10_FONT_ID, s.c_str() + start, EpdFontFamily::REGULAR);
  s[end] = saved;
  return width;
}

int KeyboardEntryActivity::lineBreakEnd(std::string& s, const int start, const int maxWidth) const {
  const int len = static_cast<int>(s.length());
  if (measureRange(s, start, len) <= maxWidth) return len;
  int lo = start + 1;
  int hi = len - 1;
  int best = start + 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    if (measureRange(s, start, mid) <= maxWidth) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }

  // The byte-index search can stop inside a character; snap back to a boundary,
  // keeping one whole character so the wrap loop always advances.
  const int firstCharEnd = static_cast<int>(utf8Next(s, static_cast<size_t>(start)));
  while (best > start && (static_cast<uint8_t>(s[best]) & 0xC0) == 0x80) best--;
  // Widths measured mid-character are unreliable, so the search can overshoot.
  while (best > firstCharEnd && measureRange(s, start, best) > maxWidth) {
    best = static_cast<int>(utf8Prev(s, static_cast<size_t>(best)));
  }
  return best < firstCharEnd ? firstCharEnd : best;
}

fui::Rect KeyboardEntryActivity::keyboardRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int rows = currentLayout().rowCount;
  const int gap = metrics.keyboardKeySpacing;
  const int keyHeight = (inputType == InputType::Url && urlPanel) ? 40 : metrics.keyboardKeyHeight;
  const int height = rows * keyHeight + (rows > 1 ? (rows - 1) * gap : 0);
  const int width = pageWidth * metrics.keyboardWidthPercent / 100;
  const int x = (pageWidth - width) / 2;
  const int y =
      pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - height + metrics.keyboardVerticalOffset - 14;
  return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width),
                   static_cast<int16_t>(height)};
}

void KeyboardEntryActivity::loop() {
  // Cursor navigation has no useful target on an empty input. Also discard
  // any pending UP hold so an empty field can never enter cursor mode later.
  if (text.empty()) {
    if (cursorMode) {
      cursorMode = false;
      togglePos = false;
      passwordVisible = false;
      cursorPos = 0;
      rightHeld = false;
      rightLongHandled = false;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    upHeld = !cursorMode;
    upLongHandled = false;
  }

  if (upHeld && !upLongHandled && !text.empty() &&
      mappedInput.isPressed(MappedInputManager::Button::Up) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    cursorMode = true;
    upLongHandled = true;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upHeld && !upLongHandled && !cursorMode) {
      moveSelectionRow(-1);
      requestUpdate();
    }
    upHeld = false;
    upLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    downHeld = true;
    if (cursorMode) {
      togglePos = false;
      passwordVisible = false;
      cursorMode = false;
      downLongHandled = true;
      requestUpdate();
    } else {
      downLongHandled = false;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downHeld && !downLongHandled && !cursorMode) {
      moveSelectionRow(1);
      requestUpdate();
    }
    downHeld = false;
    downLongHandled = false;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
    if (cursorMode) return;
    moveSelectionCol(-1);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (cursorMode) {
      if (togglePos) {
        cursorPos = savedCursorPos;
        togglePos = false;
        requestUpdate();
      } else if (cursorPos > 0) {
        cursorPos = utf8Prev(text, cursorPos);
        requestUpdate();
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (cursorMode && inputType == InputType::Password && !togglePos) {
      rightHeld = true;
      rightLongHandled = false;
      rightStartCursorPos = cursorPos;
    }
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] {
    if (cursorMode) return;
    moveSelectionCol(1);
    requestUpdate();
  });

  if (rightHeld && !rightLongHandled && mappedInput.isPressed(MappedInputManager::Button::Right) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    if (cursorMode && inputType == InputType::Password && !togglePos) {
      savedCursorPos = rightStartCursorPos;
      togglePos = true;
      rightLongHandled = true;
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (cursorMode && inputType == InputType::Password) {
      rightHeld = false;
      rightLongHandled = false;
    }
    if (cursorMode && !togglePos && cursorPos < text.length()) {
      cursorPos = utf8Next(text, cursorPos);
      requestUpdate();
    }
    if (cursorMode) return;
    rightHeld = false;
    rightLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  const fui::KeyboardKey* selKey = selectedKey();
  const bool selectedDel = selKey && selKey->value == fui::QWERTY_KEY_BACKSPACE;

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= DEL_LONG_PRESS_MS && selectedDel) {
    clearAllOrAltOnSelected();
    confirmLongHandled = true;
    requestUpdate();
  }

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    if (!selectedDel && clearAllOrAltOnSelected()) {
      requestUpdate();
      confirmLongHandled = true;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (confirmHeld && !confirmLongHandled && !cursorMode) {
      if (selKey && activateValue(selKey->value, false)) {
        requestUpdate();
      }
    } else if (confirmHeld && !confirmLongHandled && cursorMode && inputType == InputType::Password && togglePos) {
      passwordVisible = !passwordVisible;
      requestUpdate();
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onCancel();
  }

}

void KeyboardEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, pageWidth, metrics.headerHeight}, title.c_str());

  constexpr int kHintOffsetY = 0;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  // Keep the input text and its underline together, 10px below the original position.
  const int inputStartY = metrics.topPadding - 6 + metrics.headerHeight + metrics.verticalSpacing +
                          metrics.verticalSpacing * 4 + metrics.keyboardVerticalOffset + 12;
  const int inputHeight = 0;

  std::string displayText = displayTextForCurrentState();

  const bool isPassword = (inputType == InputType::Password);
  const int availableWidth = pageWidth;
  const int effectiveMargin = (pageWidth - availableWidth * metrics.keyboardTextFieldWidthPercent / 100) / 2;
  const int toggleGap = isPassword ? 4 : 0;
  const int toggleReserve = isPassword ? std::max(renderer.getTextWidth(UI_10_FONT_ID, "[abc]"),
                                                  renderer.getTextWidth(UI_10_FONT_ID, "[***]")) +
                                             toggleGap
                                       : 0;
  const int textAreaWidth = pageWidth - 2 * effectiveMargin - toggleReserve;

  // Use the entire input field. Long values are shortened from the beginning
  // with a leading ellipsis rather than being constrained by the hint below.
  const int maxLineWidth = textAreaWidth;
  const bool centerText = metrics.keyboardCenteredText;

  const size_t absoluteCursorPos = std::min(cursorPos, text.length());

  constexpr const char* kEllipsis = "…";
  const int ellipsisWidth = renderer.getTextAdvanceX(UI_10_FONT_ID, kEllipsis, EpdFontFamily::REGULAR);

  // Keep the end of long values visible by default. Reserve room for a real
  // ellipsis before selecting the largest UTF-8-safe suffix that fits.
  size_t visibleStart = 0;
  size_t visibleEnd = displayText.length();
  bool leadingEllipsis = false;
  bool trailingEllipsis = false;
  if (measureRange(displayText, 0, static_cast<int>(displayText.length())) > maxLineWidth) {
    leadingEllipsis = true;
    visibleStart = displayText.length();
    while (visibleStart > 0) {
      const size_t candidateStart = utf8Prev(displayText, visibleStart);
      const int candidateWidth =
          measureRange(displayText, static_cast<int>(candidateStart), static_cast<int>(displayText.length()));
      if (candidateWidth + ellipsisWidth > maxLineWidth) break;
      visibleStart = candidateStart;
    }
  }

  // In cursor mode, move the one-line window to the cursor when it is before
  // the visible suffix. Show ellipses at either end when text is omitted.
  if (cursorMode && absoluteCursorPos < visibleStart) {
    visibleStart = absoluteCursorPos;
    leadingEllipsis = visibleStart > 0;
    const int prefixReserve = leadingEllipsis ? ellipsisWidth : 0;
    visibleEnd = static_cast<size_t>(lineBreakEnd(
        displayText, static_cast<int>(visibleStart), std::max(1, maxLineWidth - prefixReserve)));
    if (visibleEnd < displayText.length()) {
      trailingEllipsis = true;
      visibleEnd = static_cast<size_t>(lineBreakEnd(
          displayText, static_cast<int>(visibleStart),
          std::max(1, maxLineWidth - prefixReserve - ellipsisWidth)));
    }
  }

  const std::string visibleText = displayText.substr(visibleStart, visibleEnd - visibleStart);
  const std::string visibleDisplayText =
      (leadingEllipsis ? kEllipsis : "") + visibleText + (trailingEllipsis ? kEllipsis : "");
  const bool cursorVisible = absoluteCursorPos >= visibleStart && absoluteCursorPos <= visibleEnd;
  const size_t renderCursorPos = cursorVisible ? absoluteCursorPos - visibleStart : 0;

  // The cursor spans a whole code point: a lone byte of it renders as a replacement glyph.
  // Masking is per byte, so displayText keeps text's length and the same span applies to both.
  const size_t cursorCharBytes =
      (cursorVisible && absoluteCursorPos < text.length()) ? utf8Next(text, absoluteCursorPos) - absoluteCursorPos : 0;
  char cursorChar[8] = {};         // the character under the cursor
  char displayCursorChar[8] = {};  // same span of displayText, masked for passwords
  if (cursorCharBytes > 0) {
    const size_t n = std::min(cursorCharBytes, sizeof(cursorChar) - 1);
    memcpy(cursorChar, text.data() + absoluteCursorPos, n);
    memcpy(displayCursorChar, displayText.data() + absoluteCursorPos, n);
  }

  int cursorCharWidth = 6;
  if (cursorCharBytes > 0) {
    int w = renderer.getTextWidth(UI_10_FONT_ID, cursorChar);
    if (w > cursorCharWidth) cursorCharWidth = w;
  }

  const int textWidth =
      renderer.getTextAdvanceX(UI_10_FONT_ID, visibleDisplayText.c_str(), EpdFontFamily::REGULAR);
  const int inputTextStartX = centerText ? (pageWidth - maxLineWidth) / 2 : effectiveMargin;
  int cursorPixelX = inputTextStartX;
  const int cursorLineY = inputStartY + 12;
  std::string beforeCursor;

  if (cursorVisible) {
    if (leadingEllipsis) beforeCursor = kEllipsis;
    if (isPassword && !passwordVisible && cursorMode) {
      beforeCursor += std::string(renderCursorPos, '*');
    } else {
      beforeCursor += visibleText.substr(0, renderCursorPos);
    }

    const int beforeWidth =
        renderer.getTextAdvanceX(UI_10_FONT_ID, beforeCursor.c_str(), EpdFontFamily::REGULAR);
    int kernOffset = 0;
    if (cursorCharBytes > 0) {
      const std::string beforeAndCursor = beforeCursor + displayCursorChar;
      const int beforeAndCursorWidth =
          renderer.getTextAdvanceX(UI_10_FONT_ID, beforeAndCursor.c_str(), EpdFontFamily::REGULAR);
      const int charAdvance =
          renderer.getTextAdvanceX(UI_10_FONT_ID, displayCursorChar, EpdFontFamily::REGULAR);
      kernOffset = beforeAndCursorWidth - beforeWidth - charAdvance;
    }

    const int textOffsetX = centerText ? (maxLineWidth - textWidth) / 2 : 0;
    cursorPixelX = inputTextStartX + textOffsetX + beforeWidth + kernOffset;
  }

  const int lineStartX = inputTextStartX + (centerText ? (maxLineWidth - textWidth) / 2 : 0);
  if (cursorVisible && cursorMode && isPassword && !passwordVisible && !togglePos) {
    // Draw text in 3 parts to avoid block cursor overflowing onto next char.
    // displayText uses '*' for all chars; actual char may be wider than '*'.
    const std::string part1 = (leadingEllipsis ? kEllipsis : "") + visibleText.substr(0, renderCursorPos);
    renderer.drawText(UI_10_FONT_ID, lineStartX, inputStartY + 10, part1.c_str());

    const size_t afterStart = renderCursorPos + cursorCharBytes;
    std::string part3 = visibleText.substr(afterStart);
    if (trailingEllipsis) part3 += kEllipsis;
    if (!part3.empty()) {
      renderer.drawText(UI_10_FONT_ID, cursorPixelX + cursorCharWidth, inputStartY + 10, part3.c_str());
    }
  } else {
    renderer.drawText(UI_10_FONT_ID, lineStartX, inputStartY + 10, visibleDisplayText.c_str());
  }

  const int fieldWidth = textWidth;
  const int lineMargin = effectiveMargin;
  // Draw the text field without its built-in cursor.
  // We draw our own thinner, shorter cursor below.
  GUI.drawTextField(renderer, Rect{0, inputStartY, pageWidth, inputHeight}, fieldWidth, false, lineMargin,
                    pageWidth - 2 * lineMargin);

  if (cursorVisible) {
    // Thin cursor, vertically centred against the text.
    constexpr int cursorWidth = 1;
    constexpr int cursorVerticalPadding = 2;
    const int cursorHeight = std::max(1, lineHeight - cursorVerticalPadding * 2 + 6);
    const int cursorY = cursorLineY + cursorVerticalPadding - 8;

    renderer.fillRect(cursorPixelX, cursorY, cursorWidth, cursorHeight, true);
  }

  if (isPassword) {
    const char* toggleLabel = passwordVisible ? "[***]" : "[abc]";
    const int toggleWidth = renderer.getTextWidth(UI_10_FONT_ID, toggleLabel);
    const int toggleX = pageWidth - effectiveMargin - toggleWidth;
    const int toggleY = inputStartY + inputHeight + 12;
    const bool toggleSelected = cursorMode && togglePos;

    if (toggleSelected) {
      renderer.fillRect(toggleX - 2, toggleY, toggleWidth + 5, lineHeight + 3, true);
      renderer.drawText(UI_10_FONT_ID, toggleX, toggleY, toggleLabel, false);
    } else {
      renderer.drawText(UI_10_FONT_ID, toggleX, toggleY, toggleLabel, true);
    }
  }

  if (cursorMode && !text.empty()) {
    const int hintLh = renderer.getLineHeight(UI_10_FONT_ID);
    const int underlineY = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing;
    const int hintY = underlineY + 4 + kHintOffsetY;
    if (inputType == InputType::Password && togglePos) {
      int hintLineY = hintY;
      renderer.drawCenteredText(
          UI_10_FONT_ID, hintLineY,
          passwordVisible ? tr(STR_KB_HINT_TOGGLE_HIDE_PASSWORD) : tr(STR_KB_HINT_TOGGLE_SHOW_PASSWORD), true);
      hintLineY += hintLh;
      renderer.drawCenteredText(UI_10_FONT_ID, hintLineY, tr(STR_KB_HINT_RETURN_CURSOR), true);
    } else if (inputType == InputType::Password) {
      const char* passTip = passwordVisible ? tr(STR_KB_HINT_HIDE_PASSWORD) : tr(STR_KB_HINT_SHOW_PASSWORD);
      renderer.drawCenteredText(UI_10_FONT_ID, hintY, passTip, true);
    }
  }

  const fui::Rect kbRect = keyboardRect();
  // Match the frame's horizontal inset to its 12px top inset. The
  // Shift-to-Delete row defines the keyboard's visible outer width.
  const fui::KeyboardLayout& frameLayout = currentLayout();
  int frameKeyWidth = 0;
  // The letter layout has Shift and Del on the same row. The symbol layout
  // places Shift on the bottom row and Del on the row above, so find both
  // markers across the whole layout and size the frame to the widest row.
  for (uint8_t row = 0; row < frameLayout.rowCount; ++row) {
    const fui::KeyboardRow& frameRow = frameLayout.rows[row];
    int rowWidth = 0;
    for (uint8_t col = 0; col < frameRow.count; ++col) {
      const fui::KeyboardKey& key = frameRow.keys[col];
      if (inputType == InputType::Url && urlPanel) {
        rowWidth += 100;
        continue;
      }
      switch (key.kind) {
        case fui::KeyKind::Shift:
        case fui::KeyKind::Delete:
          rowWidth += 60;
          break;
        case fui::KeyKind::Mode:
        case fui::KeyKind::Ok:
          rowWidth += 90;
          break;
        case fui::KeyKind::Space:
          rowWidth += 160;
          break;
        default:
          rowWidth += 40 * (key.widthUnits ? key.widthUnits : 1);
          break;
      }
    }
    if (frameRow.count > 1) rowWidth += (frameRow.count - 1) * metrics.keyboardKeySpacing;
    frameKeyWidth = std::max(frameKeyWidth, rowWidth);
  }
  // URL snippets have no Shift key, so size every layer from its actual
  // widest row rather than requiring the Shift/Del pair.
  const int frameWidth = frameKeyWidth + 24;
  const int frameX = (pageWidth - frameWidth) / 2;
  renderer.drawRoundedRect(frameX, kbRect.y - 12, frameWidth, kbRect.height + 24, 1, 8, true);

  const int tipsLh = renderer.getLineHeight(UI_10_FONT_ID);
  const int underlineBottom = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing + 4;

  constexpr int tipGap = 9;
  constexpr int tipsOffset = 12;

  auto drawTip = [&](const char* tip, int y) {
    renderer.drawCenteredText(UI_10_FONT_ID, y + kHintOffsetY, tip, true);
  };

  /*
   * The bottom Tips area has a fixed top position.
   *
   * This is intentional: adding "Clear text" when the user types
   * must NOT move the existing tips upward.
   */
  const int tipsStartY =
      (underlineBottom + kbRect.y) / 2 - 3 * tipsLh - 2 * tipGap - tipsOffset + 18;

  // The normal uppercase hint is stored as one long string.
  // Split it after "UPPERCASE" so the secondary part gets its own line.
  const char* altCharTip = nullptr;
  std::string altTipFirst;
  std::string altTipSecond;
  bool splitAltTip = false;

  if (!cursorMode && !urlPanel && !symbols) {
    if (inputType == InputType::Url) {
      altCharTip = tr(STR_KB_HINT_SECONDARY_CHAR);
    } else if (shifted) {
      altCharTip = tr(STR_KB_HINT_LOWER_SECONDARY);
    } else {
      altCharTip = tr(STR_KB_HINT_UPPER_SECONDARY);
    }

    if (inputType != InputType::Url && altCharTip != nullptr) {
      std::string fullTip = altCharTip;
      const std::string uppercaseMarker = "UPPERCASE";
      const std::string lowercaseMarker = "lowercase";

      size_t markerPos = fullTip.find(uppercaseMarker);
      std::string marker = uppercaseMarker;

      if (markerPos == std::string::npos) {
        markerPos = fullTip.find(lowercaseMarker);
        marker = lowercaseMarker;
      }

      if (markerPos != std::string::npos) {
        const size_t splitPos = markerPos + marker.length();

        altTipFirst = fullTip.substr(0, splitPos);
        altTipSecond = fullTip.substr(splitPos);

        while (!altTipSecond.empty() &&
               (altTipSecond.front() == ' ' ||
                altTipSecond.front() == '\t')) {
          altTipSecond.erase(altTipSecond.begin());
        }

        splitAltTip = !altTipSecond.empty();
      }
    }
  }

  int y = tipsStartY;

  if (inputType == InputType::Url && urlPanel && cursorMode) {

    drawTip(tr(STR_KB_TIPS), 324);
    drawTip("Press DOWN to return to keyboard", 352);
    drawTip("Press LEFT or RIGHT to move cursor", 380);

  } else if (inputType == InputType::Url && urlPanel) {

    // The URL snippets panel keeps the same hint block in normal mode.
    drawTip(tr(STR_KB_TIPS), 310);
    drawTip("Press ABC to exit URL mode", 338);
    if (!text.empty()) {
      drawTip("Hold UP to move cursor", 366);
      drawTip("Hold DEL to clear all text", 394);
    }

  } else if (inputType == InputType::Url && cursorMode) {

    drawTip(tr(STR_KB_TIPS), 324);
    drawTip("Press DOWN to return to keyboard", 352);
    drawTip("Press LEFT or RIGHT to move cursor", 380);

  } else if (cursorMode) {
    // Cursor-mode hints use their own fixed three-line layout.
    drawTip(tr(STR_KB_TIPS), 324);
    drawTip(tr(STR_KB_HINT_RETURN_KEYBOARD), 352);

    if (!text.empty()) {
      drawTip(tr(STR_KB_HINT_MOVE_CURSOR), 380);
    }

  } else if (symbols && inputType == InputType::Url) {

    // URL symbol mode uses the same fixed baselines as regular symbol mode.
    drawTip(tr(STR_KB_TIPS), 324);
    if (!text.empty()) {
      drawTip("Hold UP to move cursor", 352);
      drawTip("Hold DEL to clear all text", 380);
    }

  } else if (symbols) {

    // Symbol mode has a fixed three-line hint block. The two actions only
    // appear once there is text to edit.
    drawTip(tr(STR_KB_TIPS), 324);
    if (!text.empty()) {
      drawTip("Hold UP to move cursor", 352);
      drawTip(tr(STR_KB_HINT_CLEAR_TEXT), 380);
    }

  } else if (urlPanel) {

    drawTip(tr(STR_KB_TIPS), y);
    y += tipsLh + tipGap;
    drawTip(tr(STR_KB_HINT_EXIT_URL_MODE), y);
    y += tipsLh + tipGap;

    if (!text.empty()) {
      drawTip(tr(STR_KB_HINT_CLEAR_TEXT), y);
    }

  } else if (inputType == InputType::Url) {

    // URL entry has a dedicated four-line hint block.
    drawTip(tr(STR_KB_TIPS), 282);
    drawTip(tr(STR_KB_HINT_URL_SNIPPETS), 310);
    drawTip("Hold UP to move cursor", 338);
    drawTip("Hold DEL to clear all text", 366);

  } else {

    // Keep the regular text-entry hints at their requested fixed baselines.
    drawTip(tr(STR_KB_TIPS), 268);

    if (splitAltTip) {
      drawTip(altTipFirst.c_str(), 296);
      drawTip(altTipSecond.c_str(), 324);
    } else {
      drawTip(altCharTip, 296);
    }

    if (!text.empty()) {
      drawTip("Hold UP to move cursor", 352);
      drawTip(tr(STR_KB_HINT_CLEAR_TEXT), 380);
    }
  }

  // The FreeInkUI keyboard draws the keys and registers their hit rects into
  // `interactions`; loop() (the main task) routes touch snapshots against
  // that table via TouchHoldRouter, which reads the published generation
  // (routePublished()/publishedData()) — beginPublishCycle() here makes this
  // render build into the OTHER generation, so loop() never sees a
  // half-rebuilt table no matter when it runs relative to this.
  interactions.beginPublishCycle();
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  fui::Frame<48> frame(target, device, noInput, interactions);

  fui::KeyboardProps props;
  const fui::KeyboardLayout& layout = currentLayout();

  // FreeInkUI renders Delete keys as a bitmap and suppresses their label.
  // Use a display-only copy so Minuta can show "Del" while preserving the
  // original key value and the existing backspace/clear-all behaviour.
  constexpr uint8_t kMaxDisplayRows = 5;
  constexpr uint8_t kMaxDisplayKeysPerRow = 12;
  fui::KeyboardKey displayKeys[kMaxDisplayRows][kMaxDisplayKeysPerRow];
  fui::KeyboardRow displayRows[kMaxDisplayRows];
  bool canLabelDeleteKey = layout.rowCount <= kMaxDisplayRows;

  for (uint8_t row = 0; canLabelDeleteKey && row < layout.rowCount; ++row) {
    const fui::KeyboardRow& sourceRow = layout.rows[row];
    if (sourceRow.count > kMaxDisplayKeysPerRow) {
      canLabelDeleteKey = false;
      break;
    }

    for (uint8_t col = 0; col < sourceRow.count; ++col) {
      const fui::KeyboardKey& sourceKey = sourceRow.keys[col];
      displayKeys[row][col] = sourceKey;
      fui::KeyboardKey& key = displayKeys[row][col];

      // The URL snippets panel uses a 3-column grid of 100x40 keys.
      // Other layers use the standard compact keyboard dimensions.
      if (inputType == InputType::Url && urlPanel) {
        key.widthPx = 100;
      } else switch (sourceKey.kind) {
        case fui::KeyKind::Shift:
        case fui::KeyKind::Delete:
          key.widthPx = 60;
          break;
        case fui::KeyKind::Mode:
        case fui::KeyKind::Ok:
          key.widthPx = 90;
          break;
        case fui::KeyKind::Space:
          key.widthPx = 160;
          break;
        default:
          key.widthPx = static_cast<int16_t>(40 * (sourceKey.widthUnits ? sourceKey.widthUnits : 1));
          break;
      }

      // The alternate outputs remain available to long-press handling, but
      // are no longer drawn as small corner labels on the keys.
      key.alt = nullptr;

      if (key.kind == fui::KeyKind::Delete) {
        key.kind = fui::KeyKind::Normal;
        key.label = "Del";
        key.output = nullptr;
        key.alt = nullptr;
      }
    }
    displayRows[row] = {displayKeys[row], sourceRow.count, sourceRow.insetUnits};
  }

  const fui::KeyboardLayout displayLayout{displayRows, layout.rowCount};
  props.layout = canLabelDeleteKey ? &displayLayout : &layout;
  props.keyAction = ACTION_KEY;  // one action id; loop() dispatches on key value
  props.okLabel = tr(STR_OK_BUTTON);
  props.shiftLabel = tr(STR_KEY_SHIFT);
  // Match the label to the layer the mode key leads back from: the symbols
  // layer and the URL snippet panel both label it "abc" in the static tables.
  props.modeLabel =
      (symbols || (inputType == InputType::Url && urlPanel)) ? tr(STR_KEY_MODE_ABC) : tr(STR_KEY_MODE_SYMBOLS);
  props.selectedIndex = cursorMode ? -1 : static_cast<int16_t>(selectedLogicalIndex());
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.labelOffsetY = 1;
  props.gap = static_cast<int16_t>(metrics.keyboardKeySpacing);
  props.padding = fui::Insets{0, 0, 0, 0};
  // Fingers land low on the bottom row (occlusion) and there is no key below
  // to catch the miss — extend its hit band down to the button hints bar.
  const int hintsTop = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  props.bottomHitOverflow = static_cast<int16_t>(std::max(0, hintsTop - (kbRect.y + kbRect.height)));
  fui::keyboard(frame, kbRect, props);
  interactions.publish();
  interactionsReady = true;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void KeyboardEntryActivity::onComplete(std::string text) {
  setResult(KeyboardResult{std::move(text)});
  finish();
}

void KeyboardEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
