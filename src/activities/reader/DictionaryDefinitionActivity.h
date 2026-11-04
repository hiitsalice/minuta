#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/ButtonNavigator.h"

class Page;

// Dictionary definition as a read-only list: every wrapped line of the definition is one row in the UI font.
// HTML definitions (reader-dict, WordWeb) are laid out through the EPUB chapter parser; XDXF-style markup
// (Oxford, Cambridge) is parsed directly; anything else is wrapped as plain text. Every paragraph goes through
// addParagraph(), which cleans it the same way for all dictionaries.
class DictionaryDefinitionActivity final : public UiListActivity {
 public:
  explicit DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string headword,
                                        std::string definition, bool htmlDefinition = false);

  void onEnter() override;

 private:
  struct Row {
    std::string text;
    std::string boldMask;  // same length as text: '1' = drawn bold (empty when nothing is bold)
    bool bold;             // whole row bold: a section heading or a label
    bool italic;           // example sentence
    bool justify;
    int indent;            // indent of the text in px (0 = none); a thin leader line is drawn in front when > 0
  };

  int listCount() const override { return static_cast<int>(rows_.size()); }
  const char* headerTitle() const override { return title_.c_str(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void navigateButtons() override;
  void drawFooter() override;

  void buildRows();
  void rowsFromPages(const std::vector<std::unique_ptr<Page>>& pages, int fontId);
  void rowsFromPlainText(const std::string& text);
  void rowsFromXdxf(const std::string& source);
  void addParagraph(const std::string& text, bool heading, bool italic);
  void wrapInto(const std::string& text, const std::string& mask, bool bold, bool italic, int indentPx);
  int measure(const std::string& text, bool bold, bool italic) const;
  int labelWidthPx() const;
  int pageRowsEstimate() const;
  void scrollPages(int direction);
  bool canScrollUp();
  bool canScrollDown();

  const std::string headword;
  std::string title_;  // the word with each word's first letter capitalised
  // Not const: onEnter() normalizes embedded NULs (StarDict multi-type separators) to newlines.
  std::string definition;
  const bool htmlDefinition;
  bool oxfordStyle_ = false;  // Oxford markup: examples come from <ex>, every "[...]" note is bold
  bool cambridgeStyle_ = false;  // Cambridge markup: guidewords and sub-labels are headings
  bool wordWebStyle_ = false;   // WordWeb markup: part-of-speech lines are restructured
  std::string pendingNote_;   // a lone "[with obj]" waiting to open the next definition
  std::vector<Row> rows_;
  std::vector<freeink::ui::ListItem> items_;
  ButtonNavigator navigator_;
};
