#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/ButtonNavigator.h"

class Page;

// Dictionary definition as a read-only list: every wrapped line of the definition is one row in the UI font.
// HTML definitions are laid out through the EPUB chapter parser (one line per paragraph) and re-wrapped
// here at the real row width; anything else (plain text, or HTML too damaged to parse) is wrapped directly.
class DictionaryDefinitionActivity final : public UiListActivity {
 public:
  explicit DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string headword,
                                        std::string definition, bool htmlDefinition = false);

  void onEnter() override;

 private:
  struct Row {
    std::string text;
    bool heading;  // bold part-of-speech line: not indented, not justified
    bool justify;
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
  void addParagraph(const std::string& text, bool heading);
  void wrapInto(const std::string& text, int width, bool heading);
  int measure(const std::string& text, bool bold) const;
  int labelWidthPx() const;
  void scrollPages(int direction);
  bool canScrollUp();
  bool canScrollDown();

  const std::string headword;
  std::string title_;  // the word with each word's first letter capitalised
  // Not const: onEnter() normalizes embedded NULs (StarDict multi-type separators) to newlines.
  std::string definition;
  const bool htmlDefinition;
  std::vector<Row> rows_;
  std::vector<freeink::ui::ListItem> items_;
  ButtonNavigator navigator_;
};
