#include "BookmarkFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include "BookmarkUtil.h"

bool BookmarkFile::load(const std::string& bookPath, std::vector<BookmarkEntry>& bookmarks,
                         std::vector<HighlightEntry>& highlights) {
  bookmarks.clear();
  highlights.clear();

  // Read/write go through PersistableStoreBase so the JSON parser and
  // serializer stay instantiated once, in PersistableStore.cpp.
  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path.c_str(), doc)) {
    return false;
  }

  JsonArray arr = doc["bookmarks"].as<JsonArray>();
  bookmarks.reserve(arr.size());
  for (JsonObject obj : arr) {
    bookmarks.emplace_back();
    auto& bookmark = bookmarks.back();
    bookmark.xpath = obj["xpath"] | "";
    bookmark.percentage = obj["percentage"] | static_cast<float>(0);
    bookmark.summary = obj["summary"] | "";
    bookmark.computedSpineIndex = obj["si"] | static_cast<uint16_t>(0);
    bookmark.computedChapterPageCount = obj["pc"] | static_cast<uint16_t>(0);
    bookmark.computedChapterProgress = obj["pp"] | static_cast<uint16_t>(0);
    if (!obj["vo"].isNull()) {
      bookmark.visibleTextOffset = obj["vo"] | static_cast<uint32_t>(0);
      bookmark.hasVisibleTextOffset = true;
    }
  }

  JsonArray highlightArr = doc["highlights"].as<JsonArray>();
  highlights.reserve(highlightArr.size());
  for (JsonObject obj : highlightArr) {
    highlights.emplace_back();
    auto& highlight = highlights.back();
    highlight.spineIndex = obj["si"] | static_cast<uint16_t>(0);
    highlight.startVisibleTextOffset = obj["start"] | static_cast<uint32_t>(0);
    highlight.endVisibleTextOffset = obj["end"] | static_cast<uint32_t>(0);
    highlight.summary = obj["summary"] | "";
    highlight.percentage = obj["percentage"] | 0.0f;
    highlight.computedChapterPageCount = obj["pc"] | static_cast<uint16_t>(0);
    highlight.computedChapterProgress = obj["pp"] | static_cast<uint16_t>(0);
  }

  LOG_DBG("BKM", "Loaded %zu bookmarks and %zu highlights from file",
          bookmarks.size(), highlights.size());
  return true;
}

bool BookmarkFile::save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks,
                         const std::vector<HighlightEntry>& highlights) {
  JsonDocument doc;
  JsonArray arr = doc["bookmarks"].to<JsonArray>();
  LOG_DBG("BKM", "Saving %zu bookmarks to file", bookmarks.size());
  for (const auto& bookmark : bookmarks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["xpath"] = bookmark.xpath;
    obj["percentage"] = bookmark.percentage;
    obj["summary"] = bookmark.summary;
    obj["si"] = bookmark.computedSpineIndex;
    obj["pc"] = bookmark.computedChapterPageCount;
    obj["pp"] = bookmark.computedChapterProgress;
    if (bookmark.hasVisibleTextOffset) {
      obj["vo"] = bookmark.visibleTextOffset;
    }
  }

  JsonArray highlightArr = doc["highlights"].to<JsonArray>();
  for (const auto& highlight : highlights) {
    JsonObject obj = highlightArr.add<JsonObject>();
    obj["si"] = highlight.spineIndex;
    obj["start"] = highlight.startVisibleTextOffset;
    obj["end"] = highlight.endVisibleTextOffset;
    obj["summary"] = highlight.summary;
    obj["percentage"] = highlight.percentage;
    obj["pc"] = highlight.computedChapterPageCount;
    obj["pp"] = highlight.computedChapterProgress;
  }

  // writeDocToFile ensures /.crosspoint; the bookmarks subdirectory is ours.
  Storage.mkdir(BookmarkUtil::getBookmarksDir().c_str());
  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  return PersistableStoreBase::writeDocToFile(path.c_str(), doc);
}
