#pragma once

#include <string>
#include <Epub/Page.h>

#include <memory>
#include <vector>

#include "activities/Activity.h"
#include "HighlightEntry.h"

class HighlightWordSelectActivity final : public Activity {
 public:
  explicit HighlightWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       std::unique_ptr<Page> page, int marginLeft, int marginTop,
                                       uint16_t spineIndex)
      : Activity("HighlightWordSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop),
        spineIndex(spineIndex) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct WordBox {
    int16_t x;
    int16_t y;
    int16_t width;
    uint16_t row;
    std::string text;
    EpdFontFamily::Style style;
    uint32_t visibleTextOffset;
    uint32_t endVisibleTextOffset;
  };

  void extractWords();
  int closestInRow(uint16_t row, int centerX) const;
  void moveVertical(int direction);
  void confirmSelection();
  bool drawSelection();
  void drawHints() const;
  HighlightEntry saveHighlight() const;

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  const uint16_t spineIndex;

  int fontId = 0;
  int lineHeight = 0;

  std::vector<WordBox> words;
  int selected = 0;
  uint16_t rowCount = 0;

  bool selectingRange = false;
  int rangeStart = 0;

  static constexpr size_t SNAPSHOT_CAPACITY = 4096;
  std::unique_ptr<uint8_t[]> snapshot;
  int16_t snapshotX = 0;
  int16_t snapshotY = 0;
  int16_t snapshotW = 0;
  int16_t snapshotH = 0;
  int snapshotIdx = -1;
};
