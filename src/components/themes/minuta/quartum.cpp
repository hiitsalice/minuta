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
constexpr int hPadding = 60;
constexpr int columnGap = 30;
constexpr int rowGap = 30;
constexpr int titleAreaHeight = 35;
constexpr int coverWidthPx = 165;
constexpr int coverHeightPx = 275;
constexpr int vGap = 0;
constexpr int maxTitleLines = 2;
}  // namespace

void QuartumTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                       const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                       bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  // Always draw all 4 grid slots, whether or not a book fills them. A slot
  // with no book just gets an empty border - this covers 0, 1, 2, 3, or 4
  // recent books uniformly, no separate empty-state screen needed.
  const int bookCount = std::min(static_cast<int>(recentBooks.size()), 4);
  const int colWidth = coverWidthPx + columnGap;
  const int coverWidth = coverWidthPx;
  const int titleLineHeight = renderer.getLineHeight(SMALL_FONT_ID);

  const int topTitleY = rect.y;
  const int topCoverY = rect.y + titleAreaHeight;

  const int bottomCoverY = topCoverY + coverHeightPx + rowGap;
  const int bottomTitleY = bottomCoverY + coverHeightPx;

  if (!coverRendered) {
    for (int i = 0; i < 4; i++) {
      const bool isTopRow = (i < 2);
      const int col = i % 2;
      const int tileX = rect.x + hPadding + col * colWidth;
      const int coverY = isTopRow ? topCoverY : bottomCoverY;

      if (i >= bookCount) {
        // No book for this slot: just an empty border, nothing else.
        renderer.drawRect(tileX, coverY, coverWidth, coverHeightPx, true);
        continue;
      }

      bool hasCover = !recentBooks[i].coverBmpPath.empty();
      if (hasCover) {
        const std::string coverBmpPath = UITheme::getCoverThumbPath(recentBooks[i].coverBmpPath, coverHeightPx);
        HalFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            renderer.drawBitmapStretched(bitmap, tileX, coverY, coverWidth, coverHeightPx);
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
  // Empty slots (i >= bookCount) get no title text at all.
  for (int i = 0; i < bookCount; i++) {
    const bool isTopRow = (i < 2);
    const int col = i % 2;
    const int tileX = rect.x + hPadding + col * colWidth;

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
