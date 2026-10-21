#include "HighlightWordSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <cctype>
#include <climits>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "components/UITheme.h"

namespace {

bool isSelectableToken(const char* text) {
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(text); *p != 0; p++) {
    if (*p < 0x80) {
      if (std::isalnum(*p)) return true;
    } else if (*p == 0xE2 && (p[1] == 0x80 || p[1] == 0x81)) {
      if (p[2] == 0) break;
      p += 2;
    } else {
      return true;
    }
  }
  return false;
}

}  // namespace

void HighlightWordSelectActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  lineHeight = renderer.getLineHeight(fontId);
  snapshot = makeUniqueNoThrow<uint8_t[]>(SNAPSHOT_CAPACITY);

  extractWords();

  if (!words.empty()) {
    const int initial = closestInRow(rowCount / 2, renderer.getScreenWidth() / 2);
    if (initial >= 0) {
      selected = initial;
    }
  }

  requestUpdate();
}

void HighlightWordSelectActivity::extractWords() {
  words.clear();
  words.reserve(128);
  rowCount = 0;

  std::string pageText;
  pageText.reserve(2048);
  uint8_t styleMask = 0;

  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;

    const auto* line = static_cast<const PageLine*>(element.get());
    const auto& block = line->getBlock();
    if (!block || !block->valid()) continue;

    bool rowHasWords = false;
    const int ascender = renderer.getFontAscenderSize(fontId);
    const int rubyShift = block->getRubyShift(ascender);

    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const char* text = block->wordText(i);
      if (!isSelectableToken(text)) continue;

      WordBox box;
      box.x = static_cast<int16_t>(line->xPos + block->wordXpos(i) + marginLeft);
      box.y = static_cast<int16_t>(line->yPos + marginTop + rubyShift);
      box.style = block->wordStyle(i);
      box.width = 0;
      box.row = rowCount;
      box.text = text;
      box.visibleTextOffset = block->wordVisibleOffset(i);
      box.endVisibleTextOffset = block->wordVisibleEndOffset(i);

      words.push_back(std::move(box));
      rowHasWords = true;

      pageText.append(text);
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(words.back().style) & 0x03));
    }

    if (rowHasWords) rowCount++;
  }

  // Merge a word split across a line by a hyphen, exactly as Dictionary does.
  // A normal in-word hyphen remains part of the same token and is untouched.
  for (size_t i = 0; i + 1 < words.size();) {
    if (words[i].row == words[i + 1].row &&
        !words[i].text.empty() &&
        words[i].text.back() == '-') {
      words[i].text += words[i + 1].text;
      words[i].endVisibleTextOffset = words[i + 1].endVisibleTextOffset;
      words.erase(words.begin() + i + 1);
    } else {
      i++;
    }
  }

  if (styleMask == 0) styleMask = 0x01;

  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);

  // Split dash-connected tokens into selectable word parts. ASCII hyphens
  // connect compound parts; en/em dashes separate words. Dash-only fragments
  // are discarded, so no selectable item starts or ends with a dash.
  const auto utf8Length = [](const std::string& text) -> uint32_t {
    uint32_t count = 0;
    for (const unsigned char c : text) {
      if ((c & 0xC0) != 0x80) ++count;
    }
    return count;
  };

  std::vector<WordBox> splitWords;
  splitWords.reserve(words.size() * 2);

  for (const auto& original : words) {
    const std::string& text = original.text;
    struct Part {
      size_t start;
      size_t end;
    };
    std::vector<Part> parts;

    size_t start = 0;
    size_t pos = 0;
    while (pos < text.size()) {
      size_t dashLength = 0;
      const auto c = static_cast<unsigned char>(text[pos]);

      if (c == '-') {
        dashLength = 1;
      } else if (pos + 2 < text.size() && c == 0xE2 &&
                 static_cast<unsigned char>(text[pos + 1]) == 0x80 &&
                 (static_cast<unsigned char>(text[pos + 2]) == 0x93 ||
                  static_cast<unsigned char>(text[pos + 2]) == 0x94)) {
        dashLength = 3;
      }

      if (dashLength != 0) {
        if (pos > start) parts.push_back({start, pos});
        pos += dashLength;
        start = pos;
      } else {
        ++pos;
      }
    }
    if (start < text.size()) parts.push_back({start, text.size()});

    if (parts.empty()) continue;

    for (size_t p = 0; p < parts.size(); ++p) {
      const auto part = parts[p];
      WordBox item = original;
      item.text = text.substr(part.start, part.end - part.start);

      const std::string prefix = text.substr(0, part.start);
      item.x = static_cast<int16_t>(
          original.x + renderer.getTextAdvanceX(fontId, prefix.c_str(), original.style));
      item.width = static_cast<int16_t>(
          renderer.getTextAdvanceX(fontId, item.text.c_str(), item.style));
      item.visibleTextOffset = original.visibleTextOffset + utf8Length(prefix);
      item.endVisibleTextOffset = item.visibleTextOffset + utf8Length(item.text);
      item.separatorAfter.clear();
      item.separatorX = 0;
      item.separatorWidth = 0;

      if (p + 1 < parts.size()) {
        const size_t separatorStart = part.end;
        const size_t separatorEnd = parts[p + 1].start;
        item.separatorAfter = text.substr(separatorStart, separatorEnd - separatorStart);

        const std::string throughPart = text.substr(0, part.end);
        item.separatorX = static_cast<int16_t>(
            original.x + renderer.getTextAdvanceX(
                             fontId, throughPart.c_str(), original.style));
        item.separatorWidth = static_cast<int16_t>(
            renderer.getTextAdvanceX(fontId, item.separatorAfter.c_str(), original.style));
      }

      splitWords.push_back(std::move(item));
    }
  }

  words = std::move(splitWords);
}

int HighlightWordSelectActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int bestDistance = INT_MAX;

  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    if (words[i].row != row) continue;

    const int distance =
        std::abs(words[i].x + words[i].width / 2 - centerX);

    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }

  return best;
}

void HighlightWordSelectActivity::moveVertical(const int direction) {
  const WordBox& current = words[selected];
  const int targetRow = static_cast<int>(current.row) + direction;

  if (targetRow < 0 || targetRow >= static_cast<int>(rowCount)) return;

  const int best =
      closestInRow(static_cast<uint16_t>(targetRow),
                   current.x + current.width / 2);

  if (best >= 0 && best != selected) {
    selected = best;
    requestUpdate();
  }
}

void HighlightWordSelectActivity::confirmSelection() {
  if (words.empty()) return;

  if (!selectingRange) {
    rangeStart = selected;
    selectingRange = true;
    requestUpdate();
    return;
  }

  setResult(HighlightResult{saveHighlight()});
  finish();
}

void HighlightWordSelectActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmSelection();
    return;
  }

  if (words.empty()) return;

  const bool hasNextWord = selected + 1 < static_cast<int>(words.size());

  if (mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft) && selected > 0) {
    selected--;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenRight) && hasNextWord) {
    selected++;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) {
    moveVertical(1);
  }
}


bool HighlightWordSelectActivity::drawSelection() {
  if (words.empty()) {
    return false;
  }

  int first = selected;
  int last = selected;

  if (selectingRange) {
    first = std::min(rangeStart, selected);
    last = std::max(rangeStart, selected);
  }

  int hx = words[first].x - 2;
  int hy = words[first].y - 2;
  int right = words[first].x + words[first].width + 2;
  int bottom = words[first].y + lineHeight + 2;

  for (int i = first + 1; i <= last; i++) {
    hx = std::min(hx, static_cast<int>(words[i].x - 2));
    hy = std::min(hy, static_cast<int>(words[i].y - 2));
    right = std::max(right, static_cast<int>(words[i].x + words[i].width + 2));
    bottom = std::max(bottom, static_cast<int>(words[i].y + lineHeight + 2));
  }

  if (hx < 0) hx = 0;
  if (hy < 0) hy = 0;

  const int hw = right - hx;
  const int hh = bottom - hy;

  bool saved = false;
  if (snapshot && hw > 0 && hh > 0) {
    saved = renderer.readFramebufferRegion(
                hx, hy, hw, hh, snapshot.get(), SNAPSHOT_CAPACITY) > 0;
  }

  snapshotX = static_cast<int16_t>(hx);
  snapshotY = static_cast<int16_t>(hy);
  snapshotW = static_cast<int16_t>(hw);
  snapshotH = static_cast<int16_t>(hh);
  snapshotIdx = saved ? selected : -1;

  for (int i = first; i <= last; i++) {
    const auto& word = words[i];
    const int wx = word.x - 2;
    const int wy = word.y - 2;
    const int ww = word.width + 4;
    const int wh = lineHeight + 4;

    renderer.fillRect(wx, wy, ww, wh, true);
    renderer.drawText(fontId, word.x, word.y, word.text.c_str(), false, word.style);

    // A dash is highlighted only when the selection continues across it.
    if (i < last && !word.separatorAfter.empty() && word.separatorWidth > 0) {
      renderer.fillRect(word.separatorX, wy, word.separatorWidth, wh, true);
      renderer.drawText(fontId, word.separatorX, word.y,
                        word.separatorAfter.c_str(), false, word.style);
    }
  }

  return saved;
}

void HighlightWordSelectActivity::drawHints() const {
  if (words.empty()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  const auto labels = mappedInput.mapDirectionalLabels(
      tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT),
      tr(STR_DIR_RIGHT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

HighlightEntry HighlightWordSelectActivity::saveHighlight() const {
  const int first = std::min(rangeStart, selected);
  const int last = std::max(rangeStart, selected);

  HighlightEntry highlight;
  highlight.spineIndex = spineIndex;
  highlight.startVisibleTextOffset = words[first].visibleTextOffset;
  highlight.endVisibleTextOffset = words[last].endVisibleTextOffset;

  // Store the complete selected text, regardless of how many lines it spans.
  for (int i = first; i <= last; i++) {
    if (i > first) {
      if (!words[i - 1].separatorAfter.empty()) {
        highlight.summary += words[i - 1].separatorAfter;
      } else {
        highlight.summary += " ";
      }
    }
    highlight.summary += words[i].text;
  }

  return highlight;
}

void HighlightWordSelectActivity::render(RenderLock&&) {
  if (snapshotIdx >= 0 && !words.empty()) {
    const int oldFirst = selectingRange ? std::min(rangeStart, snapshotIdx) : snapshotIdx;
    const int oldLast = selectingRange ? std::max(rangeStart, snapshotIdx) : snapshotIdx;
    (void)oldFirst;
    (void)oldLast;

    renderer.writeFramebufferRegion(
        snapshotX, snapshotY, snapshotW, snapshotH, snapshot.get());
  }

  renderer.clearScreen();

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();
  page->render(renderer, fontId, marginLeft, marginTop);

  if (!words.empty()) {
    drawSelection();
  }

  drawHints();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
