#include "DictionaryDefinitionActivity.h"

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

// Rows are drawn in the list's label area, which is a little narrower than the screen; laying the text out
// 2 x 16px narrower than the screen keeps every line inside it.
constexpr int kSideMargin = 16;

// The laid-out Pages keep the whole definition resident while they are read back, so very large
// definitions take the plain-text path, which holds no page copies.
constexpr size_t kMaxStyledHtmlBytes = 16 * 1024;

// One tall layout page: the pages are only read back for their lines, so their height does not matter.
constexpr uint16_t kLayoutHeight = 4000;

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

bool isBlankByte(const char c) { return c == ' ' || c == '\t' || c == '\r'; }

}  // namespace

DictionaryDefinitionActivity::DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           std::string headword, std::string definition,
                                                           const bool htmlDefinition)
    : UiListActivity("DictionaryDefinition", renderer, mappedInput),
      headword(std::move(headword)),
      definition(std::move(definition)),
      htmlDefinition(htmlDefinition) {}

void DictionaryDefinitionActivity::onEnter() {
  // Normalize StarDict multi-type separators so every parser below sees the whole definition.
  std::replace(definition.begin(), definition.end(), '\0', '\n');
  buildRows();
  UiListActivity::onEnter();
  requestUpdate();
}

void DictionaryDefinitionActivity::pushRow(const std::string& text, bool bold, bool italic) {
  size_t first = 0;
  size_t last = text.size();
  while (first < last && isBlankByte(text[first])) first++;
  while (last > first && isBlankByte(text[last - 1])) last--;
  if (first == last) return;  // blank lines are dropped: rows stick to each other
  rows_.push_back({text.substr(first, last - first), bold, italic});
}

void DictionaryDefinitionActivity::buildRows() {
  rows_.clear();
  items_.clear();

  const int fontId = UI_10_FONT_ID;
  const int width = std::max(100, renderer.getScreenWidth() - 2 * kSideMargin);

  bool styled = false;
  if (htmlDefinition && definition.size() <= kMaxStyledHtmlBytes) {
    std::vector<std::unique_ptr<Page>> pages;
    if (buildDictionaryHtmlPages(renderer, fontId, definition, static_cast<uint16_t>(width), kLayoutHeight, pages)) {
      rowsFromPages(pages, fontId);
      styled = !rows_.empty();
    }
  }
  if (!styled) {
    rows_.clear();
    wrapPlain(htmlToPlainText(definition), fontId, width);
  }

  items_.reserve(rows_.size());
  for (size_t i = 0; i < rows_.size(); ++i) {
    fui::ListItem item;
    item.label = rows_[i].text.c_str();
    item.bold = rows_[i].bold;
    item.italic = rows_[i].italic;
    item.actionValue = static_cast<int16_t>(i);
    items_.push_back(item);
  }
}

// Reads the laid-out lines back as rows. A word that follows its neighbour without a gap (punctuation,
// styled fragments) is joined without a space; every row takes the weight of its words.
void DictionaryDefinitionActivity::rowsFromPages(const std::vector<std::unique_ptr<Page>>& pages, const int fontId) {
  const int space = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
  for (const auto& page : pages) {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = static_cast<const PageLine*>(element.get())->getBlock();
      if (!block || block->wordCount() == 0) continue;

      std::string text;
      bool allBold = true;
      bool allItalic = true;
      for (uint16_t w = 0; w < block->wordCount(); ++w) {
        const auto style = block->wordStyle(w);
        allBold = allBold && (style & EpdFontFamily::BOLD) != 0;
        allItalic = allItalic && (style & EpdFontFamily::ITALIC) != 0;
        if (w > 0) {
          const int previousEnd =
              block->wordXpos(w - 1) +
              renderer.getTextAdvanceX(fontId, block->wordText(w - 1), block->wordStyle(w - 1));
          if (block->wordXpos(w) - previousEnd >= space / 2) text += ' ';
        }
        text += block->wordText(w);
      }
      // Section headings carry bold+italic as an internal marker: they are shown as plain bold.
      if (allBold && allItalic) allItalic = false;
      pushRow(text, allBold, allItalic);
    }
  }
}

// Plain-text path: greedy word wrap. Headings are bold, a leading /pronunciation/ is italic.
void DictionaryDefinitionActivity::wrapPlain(const std::string& text, const int fontId, const int maxWidth) {
  const int space = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
  const auto measure = [&](const std::string& s) { return renderer.getTextAdvanceX(fontId, s.c_str(), EpdFontFamily::REGULAR); };

  std::string line;
  int lineWidth = 0;
  const auto flush = [&] {
    if (!line.empty()) {
      const bool pronunciation = rows_.empty() && line.size() >= 2 && line.front() == '/' && line.back() == '/';
      pushRow(line, isDictionaryHeading(line.c_str()), pronunciation);
    }
    line.clear();
    lineWidth = 0;
  };

  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    const char c = text[i];
    if (c == '\n' || c == '\0') {
      flush();
      i++;
      continue;
    }
    if (isBlankByte(c)) {
      i++;
      continue;
    }

    const size_t tokenStart = i;
    while (i < n && !isBlankByte(text[i]) && text[i] != '\n' && text[i] != '\0') i++;
    std::string token = text.substr(tokenStart, i - tokenStart);
    int tokenWidth = measure(token);

    // A token wider than a line is split at the widest fitting UTF-8 boundary.
    while (tokenWidth > maxWidth && token.size() > 1) {
      size_t cut = 0;
      for (size_t f = 1; f <= token.size(); ++f) {
        if (f == token.size() || (static_cast<unsigned char>(token[f]) & 0xC0) != 0x80) {
          if (measure(token.substr(0, f)) > maxWidth) break;
          cut = f;
        }
      }
      if (cut == 0) {
        cut = 1;
        while (cut < token.size() && (static_cast<unsigned char>(token[cut]) & 0xC0) == 0x80) cut++;
      }
      flush();
      line = token.substr(0, cut);
      flush();
      token.erase(0, cut);
      tokenWidth = measure(token);
    }
    if (token.empty()) continue;

    if (line.empty()) {
      line = token;
      lineWidth = tokenWidth;
    } else if (lineWidth + space + tokenWidth <= maxWidth) {
      line += ' ';
      line += token;
      lineWidth += space + tokenWidth;
    } else {
      flush();
      line = token;
      lineWidth = tokenWidth;
    }
  }
  flush();
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
