#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include "../activities/reader/ProgressFile.h"
#include <Txt.h>
#include <Xtc.h>

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  constexpr char EPUB_PREFIX[] = "epub_";
  constexpr char TXT_PREFIX[] = "txt_";
  constexpr char XTC_PREFIX[] = "xtc_";

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0;
}


int clearAllBookCaches() {
  LOG_DBG("BookCache", "Clearing all reading caches...");

  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    LOG_DBG("BookCache", "Failed to open cache directory");
    if (root) root.close();
    return 0;
  }

  int clearedCount = 0;
  char name[128];

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    String itemName(name);

    if (file.isDirectory() && isBookCacheDirectoryName(itemName.c_str())) {
      String fullPath = "/.crosspoint/" + itemName;
      const std::string progressPath = std::string(fullPath.c_str()) + "/progress.bin";
      uint8_t progressData[10];
      size_t progressSize = 0;

      // Reader progress is persistent state, not generated cache data.
      // Preserve it while clearing the rest of the book cache.
      HalFile progressFile;
      if (Storage.openFileForRead("BookCache", progressPath, progressFile)) {
        progressSize = progressFile.read(progressData, sizeof(progressData));
        progressFile.close();
      }

      LOG_DBG("BookCache", "Removing cache: %s", fullPath.c_str());
      file.close();

      if (Storage.removeDir(fullPath.c_str())) {
        if (!Storage.mkdir(fullPath.c_str())) {
          LOG_ERR("BookCache", "Failed to recreate cache directory: %s", fullPath.c_str());
          continue;
        }

        bool progressRestored = true;
        if (progressSize > 0) {
          progressRestored = ProgressFile::writeAtomic(fullPath.c_str(), progressData, progressSize);
          if (progressRestored) {
            LOG_DBG("BookCache", "Preserved reading progress: %s", progressPath.c_str());
          }
        }

        if (progressRestored) {
          clearedCount++;
        } else {
          LOG_ERR("BookCache", "Failed to restore progress: %s", progressPath.c_str());
        }
      } else {
        LOG_ERR("BookCache", "Failed to remove: %s", fullPath.c_str());
      }
    } else {
      file.close();
    }
  }

  root.close();

  LOG_DBG("BookCache", "Cleared %d reading caches", clearedCount);
  return clearedCount;
}

void clearBookCache(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasTxtExtension(path)) {
    Txt(path, "/.crosspoint").clearCache();
  } else {
    return;
  }
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
}
