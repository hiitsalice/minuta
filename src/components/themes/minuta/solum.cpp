#include "solum.h"

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
constexpr int hPadding = 24;   // left/right padding around the big cover
constexpr int titleGap = 12;   // gap between the cover and the title line
}  // namespace

void SolumTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                     const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                     bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const bool hasBook = !recentBooks.empty();

  const int tileX = rect.x + hPadding;
  const int tileY = rect.y;
  const int tileWidth = rect.width - 2 * hPadding;
  const int coverHeight = SolumMetrics::values.homeCoverHeight;

  if (!hasBook) {
    // No recent books yet: just an empty border where the cover would be,
    // no fill, no icon, no title text.
    renderer.drawRect(tileX, tileY, tileWidth, coverHeight, true);
    return;
  }

  const int index = std::clamp(selectorIndex, 0, static_cast<int>(recentBooks.size()) - 1);
  const RecentBook& book = recentBooks[index];

  if (!coverRendered) {
    bool hasCover = !book.coverBmpPath.empty();
    if (hasCover) {
      const std::string coverBmpPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      HalFile file;
      if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          float bmpHeight = static_cast<float>(bitmap.getHeight());
          float bmpWidth = static_cast<float>(bitmap.getWidth());
          float ratio = bmpWidth / bmpHeight;
          const float tileRatio = static_cast<float>(tileWidth) / static_cast<float>(coverHeight);
          float cropX = 1.0f - (tileRatio / ratio);
          renderer.drawBitmap(bitmap, tileX, tileY, tileWidth, coverHeight, cropX);
        } else {
          hasCover = false;
        }
        file.close();
      } else {
        hasCover = false;
      }
    }

    renderer.drawRect(tileX, tileY, tileWidth, coverHeight, true);

    if (!hasCover) {
      renderer.fillRect(tileX, tileY + coverHeight / 3, tileWidth, 2 * coverHeight / 3, true);
      renderer.drawIcon(CoverIcon, tileX + tileWidth / 2 - 16, tileY + coverHeight / 2 - 16, 32);
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // Title below the cover, single line, ellipsised ("...") if too long to fit.
  const int titleY = tileY + coverHeight + titleGap;
  const auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), tileWidth, EpdFontFamily::BOLD);
  const int titleTextWidth = renderer.getTextWidth(UI_12_FONT_ID, truncatedTitle.c_str(), EpdFontFamily::BOLD);
  const int titleX = tileX + (tileWidth - titleTextWidth) / 2;  // centered under the cover
  renderer.drawText(UI_12_FONT_ID, titleX, titleY, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);
}
