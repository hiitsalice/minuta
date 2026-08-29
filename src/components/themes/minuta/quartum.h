#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Metrics for the Quartum (4-cover) home screen: 2x2 grid of covers.
namespace QuartumMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  // Each grid cell's cover image height (not the whole tile - title text adds
  // more height on top of this, handled directly in drawRecentBookCover).
  v.homeCoverHeight = 180;
  // homeCoverTileHeight covers BOTH rows plus both title bands - drawRecentBookCover
  // computes the exact split itself, this is just the total reserved area.
  v.homeCoverTileHeight = 460;
  v.homeRecentBooksCount = 4;
  return v;
}();
}  // namespace QuartumMetrics

class QuartumTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const override;
};
