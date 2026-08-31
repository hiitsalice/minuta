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
constexpr int hPadding = 60;
constexpr int titleAreaHeight = 50;

std::string truncateTitleAtWord(const GfxRenderer& renderer, const int fontId, const std::string& title,
                                const int maxWidth) {
  if (renderer.getTextWidth(fontId, title.c_str(), EpdFontFamily::REGULAR) <= maxWidth) {
    return title;
  }

  constexpr const char* ellipsis = "...";
  std::string shortened = title;

  while (true) {
    const auto space = shortened.find_last_of(' ');
    if (space == std::string::npos) {
      return renderer.truncatedText(fontId, title.c_str(), maxWidth, EpdFontFamily::REGULAR);
    }

    shortened.erase(space);

    if (renderer.getTextWidth(fontId, (shortened + ellipsis).c_str(), EpdFontFamily::REGULAR) <= maxWidth) {
      return shortened + ellipsis;
    }
  }
}
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
          renderer.drawBitmapStretched(bitmap, tileX, tileY, tileWidth, coverHeight);
        } else {
          hasCover = false;
        }
        file.close();
      } else {
        hasCover = false;
      }
    }

    if (!hasCover) {
      renderer.fillRect(tileX, tileY + coverHeight / 3, tileWidth, 2 * coverHeight / 3, true);
      renderer.drawIcon(CoverIcon, tileX + tileWidth / 2 - 16, tileY + coverHeight / 2 - 16, 32);
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // Centre the title beneath the cover.
// The normal text baseline sits at y=710, leaving a 90px bottom margin.
const int titleBaselineY = tileY + coverHeight + titleAreaHeight;
const int titleY = titleBaselineY - renderer.getFontAscenderSize(UI_18_FONT_ID);

const auto truncatedTitle =
    truncateTitleAtWord(renderer, UI_18_FONT_ID, book.title, tileWidth);

const int titleTextWidth =
    renderer.getTextWidth(UI_18_FONT_ID, truncatedTitle.c_str(), EpdFontFamily::REGULAR);

const int titleX = tileX + (tileWidth - titleTextWidth) / 2;

renderer.drawText(
    UI_18_FONT_ID,
    titleX,
    titleY,
    truncatedTitle.c_str(),
    true,
    EpdFontFamily::REGULAR
);
}
