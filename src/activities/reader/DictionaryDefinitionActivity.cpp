#include "DictionaryDefinitionActivity.h"

#include <BoardConfig.h>
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/DictHtmlPages.h"
#include "util/HtmlToPlainText.h"

namespace fui = freeink::ui;

namespace {

// Body rows are moved this far right, with a thin line in front (like the Hidden Shortcuts rows).
constexpr int kIndent = 24;
// Text is wrapped this much narrower than the row's text area, so no line is ever cut off.
// Justification then stretches each line to the exact width. Raise it if any row still ends in "...".
constexpr int kWrapSafety = 4;
// The last line of each paragraph is left-aligned. Set to true to stretch it as well.
constexpr bool kJustifyLastLine = false;

// The HTML parser lays each paragraph out on one very wide line; the lines are wrapped again here.
constexpr uint16_t kLayoutWidth = 30000;
constexpr uint16_t kLayoutHeight = 4000;

// The laid-out Pages keep the whole definition resident while they are read back, so very large
// definitions take the plain-text path, which holds no page copies.
constexpr size_t kMaxStyledHtmlBytes = 16 * 1024;

bool isDictionaryHeading(const char* text) {
  static constexpr const char* HEADINGS[] = {
      "Noun", "Verb", "Adjective", "Adverb", "Pronoun",
      "Preposition", "Conjunction", "Interjection", "Determiner",
      "Article", "Numeral", "Particle", "Prefix", "Suffix",
      "Phrase", "Proverb", "Idiom", "Synonym", "Synonyms",
      "Antonym", "Antonyms", "Etymology", "Usage Note",
      "Usage Notes", "Alternative Forms", "Derived Terms",
      "Related Terms",
  };
  return std::any_of(std::begin(HEADINGS), std::end(HEADINGS),
                     [text](const char* heading) { return strcmp(text, heading) == 0; });
}

bool isBlankByte(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string trimmed(const std::string& s) {
  size_t first = 0;
  size_t last = s.size();
  while (first < last && isBlankByte(s[first])) first++;
  while (last > first && isBlankByte(s[last - 1])) last--;
  return s.substr(first, last - first);
}

std::string capitalizeWords(const std::string& s) {
  std::string out = s;
  bool wordStart = true;
  for (char& c : out) {
    if (c == ' ' || c == '\t') {
      wordStart = true;
      continue;
    }
    if (wordStart && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    wordStart = false;
  }
  return out;
}

}  // namespace

DictionaryDefinitionActivity::DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           std::string headword, std::string definition,
                                                           const bool htmlDefinition)
    : UiListActivity("DictionaryDefinition", renderer, mappedInput),
      headword(std::move(headword)),
      title_(capitalizeWords(this->headword)),
      definition(std::move(definition)),
      htmlDefinition(htmlDefinition) {}

void DictionaryDefinitionActivity::onEnter() {
  // Normalize StarDict multi-type separators so every parser below sees the whole definition.
  std::replace(definition.begin(), definition.end(), '\0', '\n');
  buildRows();
  UiListActivity::onEnter();
  requestUpdate();
}

int DictionaryDefinitionActivity::measure(const std::string& text, const bool bold) const {
  return renderer.getTextAdvanceX(UI_10_FONT_ID, text.c_str(), bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

// The width a list row gives to its label, worked out from the same theme numbers the list uses:
// the row inset and side padding on both sides, and the room the scroll bar takes when the inset is smaller.
int DictionaryDefinitionActivity::labelWidthPx() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const auto& insets = BoardConfig::ACTIVE.viewableInsets;
  const int scrollInset = m.listScrollSide == 1 ? insets.left : insets.right;
  const int needed = m.listScrollWidth + scrollInset + 2;
  const int cut = std::max(0, needed - m.listInset);
  return renderer.getScreenWidth() - 2 * (m.listInset + m.listSidePadding) - cut;
}

void DictionaryDefinitionActivity::buildRows() {
  rows_.clear();
  items_.clear();

  bool styled = false;
  if (htmlDefinition && definition.size() <= kMaxStyledHtmlBytes) {
    std::vector<std::unique_ptr<Page>> pages;
    if (buildDictionaryHtmlPages(renderer, UI_10_FONT_ID, definition, kLayoutWidth, kLayoutHeight, pages)) {
      rowsFromPages(pages, UI_10_FONT_ID);
      styled = !rows_.empty();
    }
  }
  if (!styled) {
    rows_.clear();
    rowsFromPlainText(htmlToPlainText(definition));
  }

  items_.reserve(rows_.size());
  for (size_t i = 0; i < rows_.size(); ++i) {
    fui::ListItem item;
    item.label = rows_[i].text.c_str();
    item.bold = rows_[i].heading;
    item.justify = rows_[i].justify;
    item.actionValue = static_cast<int16_t>(i);
    if (!rows_[i].heading) {
      item.labelXOffset = kIndent;
      item.leaderLine = true;
    }
    items_.push_back(item);
  }
}

// Reads the laid-out lines back as paragraphs. A word that follows its neighbour without a gap (punctuation,
// styled fragments) is joined without a space; a paragraph whose words are all bold is a heading.
void DictionaryDefinitionActivity::rowsFromPages(const std::vector<std::unique_ptr<Page>>& pages, const int fontId) {
  const int space = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
  for (const auto& page : pages) {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = static_cast<const PageLine*>(element.get())->getBlock();
      if (!block || block->wordCount() == 0) continue;

      std::string text;
      bool allBold = true;
      for (uint16_t w = 0; w < block->wordCount(); ++w) {
        allBold = allBold && (block->wordStyle(w) & EpdFontFamily::BOLD) != 0;
        if (w > 0) {
          const int previousEnd =
              block->wordXpos(w - 1) +
              renderer.getTextAdvanceX(fontId, block->wordText(w - 1), block->wordStyle(w - 1));
          if (block->wordXpos(w) - previousEnd >= space / 2) text += ' ';
        }
        text += block->wordText(w);
      }
      addParagraph(text, allBold);
    }
  }
}

void DictionaryDefinitionActivity::rowsFromPlainText(const std::string& text) {
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string paragraph = trimmed(text.substr(start, end - start));
    if (!paragraph.empty()) addParagraph(paragraph, isDictionaryHeading(paragraph.c_str()));
    if (end == text.size()) break;
    start = end + 1;
  }
}

void DictionaryDefinitionActivity::addParagraph(const std::string& raw, const bool heading) {
  const std::string text = trimmed(raw);
  if (text.empty()) return;
  // A line wrapped in slashes is a pronunciation (IPA): dropped, so the next line moves up.
  if (text.size() >= 2 && text.front() == '/' && text.back() == '/') return;

  const int width = std::max(80, labelWidthPx() - kWrapSafety - (heading ? 0 : kIndent));
  wrapInto(text, width, heading);
}

// Greedy word wrap at an exact pixel width. Every row of a paragraph except the last is justified.
void DictionaryDefinitionActivity::wrapInto(const std::string& text, const int width, const bool heading) {
  std::vector<std::string> lines;
  std::string line;
  const auto flush = [&] {
    if (!line.empty()) lines.push_back(line);
    line.clear();
  };

  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    while (i < n && isBlankByte(text[i])) i++;
    if (i >= n) break;
    const size_t start = i;
    while (i < n && !isBlankByte(text[i])) i++;
    std::string token = text.substr(start, i - start);

    // A word wider than a whole line is split at the widest fitting UTF-8 boundary.
    while (measure(token, heading) > width && token.size() > 1) {
      size_t cut = 0;
      for (size_t f = 1; f <= token.size(); ++f) {
        if (f == token.size() || (static_cast<unsigned char>(token[f]) & 0xC0) != 0x80) {
          if (measure(token.substr(0, f), heading) > width) break;
          cut = f;
        }
      }
      if (cut == 0) {
        cut = 1;
        while (cut < token.size() && (static_cast<unsigned char>(token[cut]) & 0xC0) == 0x80) cut++;
      }
      flush();
      lines.push_back(token.substr(0, cut));
      token.erase(0, cut);
    }
    if (token.empty()) continue;

    const std::string candidate = line.empty() ? token : line + ' ' + token;
    if (line.empty() || measure(candidate, heading) <= width) {
      line = candidate;
    } else {
      flush();
      line = token;
    }
  }
  flush();

  for (size_t k = 0; k < lines.size(); ++k) {
    const bool last = k + 1 == lines.size();
    rows_.push_back({lines[k], heading, !heading && (kJustifyLastLine || !last)});
  }
}

void DictionaryDefinitionActivity::buildScreen(UiScreen& screen) {
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 3), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  fui::ListProps props;
  props.items = items_.data();
  props.count = static_cast<uint16_t>(items_.size());
  props.action = ACTION_ROW;
  props.valueInset = 3;
  props.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  props.rowGap = 0;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  props.labelText.lineGap = 6;
  syncListViewport(screen, props);
  props.selectedIndex = -1;  // view-only: no highlight
  screen.list(props);
}

void DictionaryDefinitionActivity::activateIndex(const int) { app.clearTapFlash(); }

bool DictionaryDefinitionActivity::canScrollUp() { return activeNav().top > 0; }

bool DictionaryDefinitionActivity::canScrollDown() {
  return activeNav().top + static_cast<int>(activeNav().pageRows()) < listCount();
}

// One screenful, keeping the last row of the old view as the first row of the new one.
void DictionaryDefinitionActivity::scrollPages(const int direction) {
  auto& n = activeNav();
  const int step = std::max(1, static_cast<int>(n.pageRows()) - 1);
  bool moved = false;
  {
    RenderLock lock(*this);
    moved = n.scrollBy(direction * step, listCount());
    if (moved) n.selected = std::clamp(n.top, 0, std::max(0, listCount() - 1));
  }
  if (moved) requestUpdate();
}

void DictionaryDefinitionActivity::navigateButtons() {
  navigator_.onNext([this] { scrollPages(1); });
  navigator_.onPrevious([this] { scrollPages(-1); });
}

void DictionaryDefinitionActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", canScrollUp() ? "Prev" : "", canScrollDown() ? "Next" : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
