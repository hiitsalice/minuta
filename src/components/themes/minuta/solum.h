#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Metrics for the Solum (1-cover) home screen: same look as Lyra everywhere
// else, but the home screen shows one big cover instead of Lyra's list.
namespace SolumMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  // Solum home layout: cover sits slightly higher, with room below for
  // a 16pt title and 10pt author line.
v.homeTopPadding = 51;
v.homeCoverTileHeight = 675;
v.homeCoverHeight = 600;
v.homeRecentBooksCount = 1;
  return v;
}();
}  // namespace SolumMetrics

class SolumTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const override;
};
