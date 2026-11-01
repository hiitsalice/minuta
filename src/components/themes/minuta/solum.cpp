#include "solum.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int coverWidth = 360;
constexpr int textAreaWidth = 400;
constexpr int titleLineGapp = 4;
constexpr int authorGap = 8;

std::vector<std::string> wrapTitle(const GfxRenderer& renderer, const std::string& title,
                                   const int maxWidth) {
  constexpr int fontId = UI_12_FONT_ID;
  constexpr auto style = EpdFontFamily::BOLD;

  std::istringstream stream(title);
  std::vector<std::string> lines;
  std::string word;

  while (stream >> word) {
    if (lines.empty()) {
      lines.push_back(word);
      if (renderer.getTextWidth(fontId, lines.back().c_str(), style) > maxWidth) {
        lines.back() = renderer.truncatedText(fontId, lines.back().c_str(), maxWidth, style);
      }
      continue;
    }

    const std::string candidate = lines.back() + " " + word;
    if (renderer.getTextWidth(fontId, candidate.c_str(), style) <= maxWidth) {
      lines.back() = candidate;
      continue;
    }

    if (lines.size() == 1) {
      lines.push_back(word);
      if (renderer.getTextWidth(fontId, lines.back().c_str(), style) > maxWidth) {
        lines.back() = renderer.truncatedText(fontId, lines.back().c_str(), maxWidth, style);
        break;
      }
      continue;
    }

    // The second line is full. Truncate it with an ellipsis and stop.
    lines.back() = renderer.truncatedText(fontId, candidate.c_str(), maxWidth, style);
    break;
  }

  if (lines.empty()) {
    lines.emplace_back();
  }

  return lines;
}
}  // namespace

void SolumTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                     const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                     bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const bool hasBook = !recentBooks.empty();

  const int tileWidth = coverWidth;
  const int tileX = rect.x + (rect.width - tileWidth) / 2;
  const int tileY = rect.y;
  const int coverHeight = SolumMetrics::values.homeCoverHeight;

  if (!hasBook) {
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

  // Choose the text arrangement according to the title's line count.
  const int textAreaX = rect.x + (rect.width - textAreaWidth) / 2;
  auto titleLines = wrapTitle(renderer, book.title, textAreaWidth);

  const bool isTwoLineTitle = titleLines.size() >= 2;
  const int titleMaxWidth = isTwoLineTitle ? 300 : textAreaWidth;

  if (isTwoLineTitle) {
    titleLines = wrapTitle(renderer, book.title, titleMaxWidth);
  }

  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int authorLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const bool hasAuthor = !book.author.empty();

  constexpr int coverTextGap = 18;
  constexpr int authorCoverGap = 9;
  constexpr int authorTitleGap = 6;
  const int titleLineGap = isTwoLineTitle ? 9 : 0;

  // One line: cover -> title -> author.
  // Two lines: author -> cover -> title line 1 -> title line 2.
  const int titleTop = tileY + coverHeight + coverTextGap;

  const int authorY = isTwoLineTitle
      ? tileY - authorCoverGap - authorLineHeight
      : titleTop + titleLineHeight + authorTitleGap;

  if (hasAuthor) {
    const auto truncatedAuthor =
        renderer.truncatedText(UI_12_FONT_ID, book.author.c_str(),
                               textAreaWidth, EpdFontFamily::ITALIC);
    const int authorWidth =
        renderer.getTextWidth(UI_12_FONT_ID, truncatedAuthor.c_str(),
                              EpdFontFamily::ITALIC);
    const int authorX = textAreaX + (textAreaWidth - authorWidth) / 2;

    renderer.drawText(UI_12_FONT_ID, authorX, authorY,
                      truncatedAuthor.c_str(), true, EpdFontFamily::ITALIC);
  }

  for (size_t i = 0; i < titleLines.size(); ++i) {
    const int titleWidth =
        renderer.getTextWidth(UI_12_FONT_ID, titleLines[i].c_str(),
                              EpdFontFamily::BOLD);
    const int titleX = textAreaX + (textAreaWidth - titleWidth) / 2;
    const int titleY =
        titleTop + static_cast<int>(i) * (titleLineHeight + titleLineGap);

    renderer.drawText(UI_12_FONT_ID, titleX, titleY,
                      titleLines[i].c_str(), true, EpdFontFamily::BOLD);
  }
}
