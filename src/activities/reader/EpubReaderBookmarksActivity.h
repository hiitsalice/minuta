#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "../../BookmarkEntry.h"
#include "../../HighlightEntry.h"
#include "activities/UiListActivity.h"

class EpubReaderBookmarksActivity final : public UiListActivity {
  // The list rides the UiListActivity scaffold (themed rows and touch routing);
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  std::vector<BookmarkEntry> bookmarks;
  std::vector<HighlightEntry> highlights;

  struct SavedRow {
    bool isHighlight;
    int index;
  };

  std::vector<SavedRow> savedRows;

  // Row buffers derived from saved items, rebuilt only when they change
  // (onEnter() load, post-delete) instead of on every repaint — buildScreen()
  // used to re-compose a percentage/chapter/TOC-title subtitle string per
  // bookmark on every render (cursor move, tap flash, ...).
  std::vector<std::string> savedSubtitles;
  std::vector<freeink::ui::ListItem> savedRowItems;
  void rebuildSavedRowItems();

 public:
  explicit EpubReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::shared_ptr<Epub>& epub, const std::string& epubPath);
  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(savedRows.size()); }
  void buildScreen(UiScreen& screen) override;
  void drawChrome() override;
  void activateIndex(int index) override;
  // Back cancels with a result; Confirm opens on RELEASE (a hold is "delete").
  bool handleButtons() override;

  // Open the selected bookmark: finishes with a ProgressChangeResult for the reader.
  void openSelectedItem();

  // Delete the currently selected bookmark and persist the list
  void deleteSelectedItem();
};
