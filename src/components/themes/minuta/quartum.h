#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Metrics for the Quartum (4-cover) home screen: 2x2 grid of covers.
namespace QuartumMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeTopPadding = 60;
  v.homeCoverHeight = 275;
  v.homeCoverTileHeight = 650;
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
