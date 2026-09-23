#pragma once
#include <cstdint>
#include <string>

// A single highlight entry — a text range within a book spine.
struct HighlightEntry {
  uint16_t spineIndex = 0;
  uint32_t startVisibleTextOffset = 0;
  uint32_t endVisibleTextOffset = 0;
  std::string summary;

  float percentage = 0.0f;
  uint16_t computedChapterPageCount = 0;
  uint16_t computedChapterProgress = 0;
};
