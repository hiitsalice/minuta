#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Metrics for the Solum (1-cover) home screen: same look as Lyra everywhere
// else, but the home screen shows one big cover instead of Lyra's list.
namespace SolumMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  // Big single cover: tall tile, only 1 book shown on the home tile at a time.
  v.homeCoverTileHeight = 460;
  v.homeCoverHeight = 460;
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
