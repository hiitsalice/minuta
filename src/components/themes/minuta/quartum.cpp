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

      if (!hasCover) {
        renderer.fillRect(tileX, coverY + coverHeightPx / 3, coverWidth, 2 * coverHeightPx / 3, true);
        renderer.drawIcon(CoverIcon, tileX + coverWidth / 2 - 16, coverY + coverHeightPx / 2 - 16, 32);
      }
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // Draw an outline only around the currently selected book.
if (bookCount > 0 && selectorIndex >= 0 && selectorIndex < bookCount) {
  const bool isTopRow = (selectorIndex < 2);
  const int col = selectorIndex % 2;

  const int tileX = rect.x + hPadding + col * colWidth;
  const int coverY = isTopRow ? topCoverY : bottomCoverY;

  renderer.drawRect(tileX, coverY, coverWidth, coverHeightPx, true);
}

  // Draw one centred title for each occupied slot.
// Top-row titles sit against the 60px top margin.
// Bottom-row titles use a baseline at y=710, matching Solum.
for (int i = 0; i < bookCount; i++) {
  const bool isTopRow = (i < 2);
  const int col = i % 2;
  const int tileX = rect.x + hPadding + col * colWidth;

  const auto title =
      renderer.truncatedText(
          SMALL_FONT_ID,
          recentBooks[i].title.c_str(),
          coverWidth,
          EpdFontFamily::REGULAR
      );

  const int titleWidth =
      renderer.getTextWidth(
          SMALL_FONT_ID,
          title.c_str(),
          EpdFontFamily::REGULAR
      );

  const int titleX = tileX + (coverWidth - titleWidth) / 2;

  int titleY;

  if (isTopRow) {
    titleY = topTitleY;
  } else {
    const int titleBaselineY = bottomTitleY + titleAreaHeight;
    titleY = titleBaselineY - renderer.getFontAscenderSize(SMALL_FONT_ID);
  }

  renderer.drawText(
      SMALL_FONT_ID,
      titleX,
      titleY,
      title.c_str(),
      true,
      EpdFontFamily::REGULAR
  );
}
}
