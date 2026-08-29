#include "quartum.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int hPadding = 16;      // gap between the two columns and screen edges
constexpr int vGap = 10;          // gap between title text and its cover
constexpr int rowGap = 16;        // gap between the top row and bottom row
constexpr int coverHeightPx = 170;  // height of each cover image
constexpr int maxTitleLines = 2;
}  // namespace

void QuartumTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                       const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                       bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const bool hasBooks = !recentBooks.empty();

  if (!hasBooks) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = std::min(static_cast<int>(recentBooks.size()), 4);
  const int colWidth = (rect.width - 2 * hPadding) / 2;
  const int coverWidth = colWidth - hPadding;
  const int titleLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int titleBandHeight = titleLineHeight * maxTitleLines;

  // Top row: title band, then gap, then cover.
  const int topTitleY = rect.y;
  const int topCoverY = topTitleY + titleBandHeight + vGap;
  // Bottom row: cover, then gap, then title band.
  const int bottomCoverY = topCoverY + coverHeightPx + rowGap;
  const int bottomTitleY = bottomCoverY + coverHeightPx + vGap;

  if (!coverRendered) {
    for (int i = 0; i < bookCount; i++) {
      const bool isTopRow = (i < 2);
      const int col = i % 2;
      const int tileX = hPadding + col * colWidth;
      const int coverY = isTopRow ? topCoverY : bottomCoverY;

      bool hasCover = !recentBooks[i].coverBmpPath.empty();
      if (hasCover) {
        const std::string coverBmpPath = UITheme::getCoverThumbPath(recentBooks[i].coverBmpPath, coverHeightPx);
        HalFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            float bmpHeight = static_cast<float>(bitmap.getHeight());
            float bmpWidth = static_cast<float>(bitmap.getWidth());
            float ratio = bmpWidth / bmpHeight;
            const float tileRatio = static_cast<float>(coverWidth) / static_cast<float>(coverHeightPx);
            float cropX = 1.0f - (tileRatio / ratio);
            renderer.drawBitmap(bitmap, tileX, coverY, coverWidth, coverHeightPx, cropX);
          } else {
            hasCover = false;
          }
          file.close();
        } else {
          hasCover = false;
        }
      }

      renderer.drawRect(tileX, coverY, coverWidth, coverHeightPx, true);

      if (!hasCover) {
        renderer.fillRect(tileX, coverY + coverHeightPx / 3, coverWidth, 2 * coverHeightPx / 3, true);
        renderer.drawIcon(CoverIcon, tileX + coverWidth / 2 - 16, coverY + coverHeightPx / 2 - 16, 32);
      }
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // Titles: top row grows UPWARD (bottom-aligned to sit right above the
  // cover), bottom row grows DOWNWARD (top-aligned to sit right below).
  for (int i = 0; i < bookCount; i++) {
    const bool isTopRow = (i < 2);
    const int col = i % 2;
    const int tileX = hPadding + col * colWidth;

    auto titleLines = renderer.wrappedText(SMALL_FONT_ID, recentBooks[i].title.c_str(), coverWidth, maxTitleLines);
    const int blockHeight = static_cast<int>(titleLines.size()) * titleLineHeight;

    int lineY;
    if (isTopRow) {
      // Bottom-align the text block against the top of its cover.
      lineY = topCoverY - vGap - blockHeight;
    } else {
      // Top-align the text block against the bottom of its cover.
      lineY = bottomTitleY;
    }

    for (const auto& line : titleLines) {
      const int lineWidth = renderer.getTextWidth(SMALL_FONT_ID, line.c_str());
      const int lineX = tileX + (coverWidth - lineWidth) / 2;  // centered under/over the cover
      renderer.drawText(SMALL_FONT_ID, lineX, lineY, line.c_str(), true);
      lineY += titleLineHeight;
    }
  }
}
