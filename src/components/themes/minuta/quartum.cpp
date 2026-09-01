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
constexpr int columnGap = 18;
constexpr int rowGap = 18;
constexpr int titleAreaHeight = 35;
constexpr int coverWidthPx = 171;
constexpr int coverHeightPx = 285;
constexpr int vGap = 0;

std::vector<std::string> wrapTitleAtWords(const GfxRenderer& renderer, const int fontId,
                                          const std::string& title, const int maxWidth) {
  constexpr auto style = EpdFontFamily::REGULAR;
  constexpr const char* ellipsis = "...";

  std::vector<std::string> words;
  size_t pos = 0;

  while (pos < title.size()) {
    while (pos < title.size() && title[pos] == ' ') {
      ++pos;
    }
    if (pos >= title.size()) {
      break;
    }

    const size_t end = title.find(' ', pos);
    words.push_back(title.substr(pos, end == std::string::npos ? std::string::npos : end - pos));

    if (end == std::string::npos) {
      break;
    }
    pos = end + 1;
  }

  if (words.empty()) {
    return {};
  }

  std::vector<std::string> lines;
  std::string first;
  size_t wordIndex = 0;

  while (wordIndex < words.size()) {
    const std::string candidate = first.empty() ? words[wordIndex] : first + " " + words[wordIndex];

    if (renderer.getTextWidth(fontId, candidate.c_str(), style) > maxWidth) {
      break;
    }

    first = candidate;
    ++wordIndex;
  }

  if (first.empty()) {
    first = renderer.truncatedText(fontId, words[0].c_str(), maxWidth, style);
    wordIndex = 1;
  }

  lines.push_back(first);

  if (wordIndex >= words.size()) {
    return lines;
  }

  std::string remaining;
  for (size_t i = wordIndex; i < words.size(); ++i) {
    if (!remaining.empty()) {
      remaining += " ";
    }
    remaining += words[i];
  }

  if (renderer.getTextWidth(fontId, remaining.c_str(), style) <= maxWidth) {
    lines.push_back(remaining);
    return lines;
  }

  std::string second;

  while (wordIndex < words.size()) {
    const std::string candidate = second.empty() ? words[wordIndex] : second + " " + words[wordIndex];
    const bool moreWordsRemain = wordIndex + 1 < words.size();
    const std::string measured = candidate + (moreWordsRemain ? ellipsis : "");

    if (renderer.getTextWidth(fontId, measured.c_str(), style) > maxWidth) {
      break;
    }

    second = candidate;
    ++wordIndex;
  }

  if (second.empty()) {
    lines.push_back(renderer.truncatedText(fontId, remaining.c_str(), maxWidth, style));
  } else {
    lines.push_back(second + ellipsis);
  }

  return lines;
}
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

  constexpr int contentYOffset = 4;

  const int topTitleY = rect.y + contentYOffset;
  const int topCoverY = rect.y + contentYOffset + titleAreaHeight;

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

  // Double inset outline so selection remains visible over the cover itself.
  renderer.drawRect(tileX + 1, coverY + 1, coverWidth - 2, coverHeightPx - 2, true);
  renderer.drawRect(tileX + 2, coverY + 2, coverWidth - 4, coverHeightPx - 4, true);
}

  // Draw up to two centred title lines for each occupied slot.
  // One-line titles stay close to the cover; two-line titles expand away from it.
  constexpr int titleLineGap = 6;
  constexpr int titleCoverGap = 9;
  constexpr int titleMaxWidth = 174;
  const int titleLineHeight = renderer.getTextHeight(UI_11_FONT_ID);

  for (int i = 0; i < bookCount; i++) {
    const bool isTopRow = (i < 2);
    const int col = i % 2;
    const int tileX = rect.x + hPadding + col * colWidth;
    const int coverY = isTopRow ? topCoverY : bottomCoverY;

    const auto lines =
        wrapTitleAtWords(renderer, UI_11_FONT_ID, recentBooks[i].title, titleMaxWidth);

    if (lines.empty()) {
      continue;
    }

    const int blockHeight =
        static_cast<int>(lines.size()) * titleLineHeight +
        (static_cast<int>(lines.size()) - 1) * titleLineGap;

    int titleY;

    if (isTopRow) {
      titleY = topCoverY - blockHeight - titleCoverGap;
    } else {
      titleY = bottomTitleY + titleCoverGap;
    }

    for (const auto& line : lines) {
      const int titleWidth =
          renderer.getTextWidth(UI_11_FONT_ID, line.c_str(), EpdFontFamily::REGULAR);
      const int titleX = tileX + (coverWidth - titleWidth) / 2;

      renderer.drawText(
          UI_11_FONT_ID,
          titleX,
          titleY,
          line.c_str(),
          true,
          EpdFontFamily::REGULAR
      );

      titleY += titleLineHeight + titleLineGap;
    }

    // Author: one italic line running vertically beside the cover.
    // Left column = 270 degrees on the outside left.
    // Right column = 90 degrees on the outside right.
    if (!recentBooks[i].author.empty()) {
      const auto author =
          renderer.truncatedText(
              UI_11_FONT_ID,
              recentBooks[i].author.c_str(),
              coverHeightPx,
              EpdFontFamily::ITALIC);

      // Anchor the end of the vertical author line to the cover's bottom edge.
      const int authorBottomY = coverY + coverHeightPx - 1;

      if (col == 1) {
        // Right-hand books: 90 degrees, reading in the opposite direction
        // from the 270-degree authors on the left.
        const int authorLeftX =
            tileX + coverWidth + titleCoverGap;

        const int authorTextWidth =
            renderer.getTextWidth(
                UI_11_FONT_ID,
                author.c_str(),
                EpdFontFamily::ITALIC);

        const int authorBandWidth =
            renderer.getTextHeight(UI_11_FONT_ID);

        // Keep the bottom of the author aligned with the bottom of the cover.
        const int authorTopY =
            isTopRow
                ? coverY
                : authorBottomY - authorTextWidth;

        const auto originalOrientation = renderer.getOrientation();
        const int portraitWidth = renderer.getScreenWidth();

        // LandscapeCounterClockwise lets ordinary horizontal text appear as
        // 90-degree vertical text in portrait coordinates.
        const int rotatedX = authorTopY;
        const int rotatedY =
            portraitWidth - authorLeftX - authorBandWidth;

        renderer.setOrientation(
            GfxRenderer::Orientation::LandscapeCounterClockwise);

        renderer.drawText(
            UI_11_FONT_ID,
            rotatedX,
            rotatedY,
            author.c_str(),
            true,
            EpdFontFamily::ITALIC
        );

        renderer.setOrientation(originalOrientation);
      } else {
        // Left-hand books: 270-degree author.
        //
        // GfxRenderer has a 90-degree helper but no matching 270-degree
        // helper, so draw in LandscapeClockwise coordinates temporarily.
        const auto originalOrientation = renderer.getOrientation();

        const int portraitHeight = renderer.getScreenHeight();
        const int authorBandWidth =
            renderer.getTextHeight(UI_11_FONT_ID);

        // Keep the visible author band the same 9 px away from the cover.
        const int authorLeftX =
            tileX - titleCoverGap - authorBandWidth;

        // Convert the desired portrait-space anchor into
        // LandscapeClockwise coordinates.
        const int authorAnchorY =
            isTopRow
                ? coverY + renderer.getTextWidth(
                               UI_11_FONT_ID,
                               author.c_str(),
                               EpdFontFamily::ITALIC)
                : authorBottomY;

        const int rotatedX =
            portraitHeight - 1 - authorAnchorY;
        const int rotatedY =
            authorLeftX;

        renderer.setOrientation(
            GfxRenderer::Orientation::LandscapeClockwise);

        renderer.drawText(
            UI_11_FONT_ID,
            rotatedX,
            rotatedY,
            author.c_str(),
            true,
            EpdFontFamily::ITALIC
        );

        renderer.setOrientation(originalOrientation);
      }
    }
  }
}
