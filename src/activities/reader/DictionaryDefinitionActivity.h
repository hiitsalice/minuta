#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/ButtonNavigator.h"

class Page;

// Dictionary definition as a read-only list: every line of the definition is one row, shown in the UI font.
// HTML definitions are laid out through the EPUB chapter parser and read back line by line; anything else
// (plain text, or HTML too damaged to parse) is word-wrapped directly.
class DictionaryDefinitionActivity final : public UiListActivity {
 public:
  explicit DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string headword,
                                        std::string definition, bool htmlDefinition = false);

  void onEnter() override;

 private:
  struct Row {
    std::string text;
    bool bold;
    bool italic;
  };

  int listCount() const override { return static_cast<int>(rows_.size()); }
  const char* headerTitle() const override { return headword.c_str(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void navigateButtons() override;
  void drawFooter() override;

  void buildRows();
  void rowsFromPages(const std::vector<std::unique_ptr<Page>>& pages, int fontId);
  void wrapPlain(const std::string& text, int fontId, int maxWidth);
  void pushRow(const std::string& text, bool bold, bool italic);
  void scrollPages(int direction);
  bool canScrollUp();
  bool canScrollDown();

  const std::string headword;
  // Not const: onEnter() normalizes embedded NULs (StarDict multi-type separators) to newlines.
  std::string definition;
  const bool htmlDefinition;
  std::vector<Row> rows_;
  std::vector<freeink::ui::ListItem> items_;
  ButtonNavigator navigator_;
};
