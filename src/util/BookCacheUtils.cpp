#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include "../activities/reader/ProgressFile.h"
#include <Txt.h>
#include <Xtc.h>
#include <utility>
#include <vector>

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

    if (!file.isDirectory() || !isBookCacheDirectoryName(itemName.c_str())) {
      file.close();
      continue;
    }

    const std::string cachePath = std::string("/.crosspoint/") + itemName.c_str();
    file.close();

    bool removedCacheData = false;
    std::vector<std::string> stack;
    stack.push_back(cachePath);

    while (!stack.empty()) {
      const std::string currentPath = std::move(stack.back());
      stack.pop_back();

      auto dir = Storage.open(currentPath.c_str());
      if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        LOG_ERR("BookCache", "Failed to open cache directory: %s", currentPath.c_str());
        continue;
      }

      dir.rewindDirectory();
      for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        char entryName[128];
        entry.getName(entryName, sizeof(entryName));

        if (strcmp(entryName, ".") == 0 || strcmp(entryName, "..") == 0) {
          entry.close();
          continue;
        }

        std::string entryPath = currentPath + "/" + entryName;
        const bool isDir = entry.isDirectory();
        entry.close();

        if (isDir) {
          stack.push_back(entryPath);
        } else if (strcmp(entryName, "progress.bin") != 0) {
          if (Storage.remove(entryPath.c_str())) {
            removedCacheData = true;
          } else {
            LOG_ERR("BookCache", "Failed to remove cache file: %s", entryPath.c_str());
          }
        }
      }

      dir.close();

      // Remove empty cache subdirectories after their contents have been cleared.
      // The top-level book cache directory is intentionally retained for progress.bin.
      if (currentPath != cachePath && !Storage.removeDir(currentPath.c_str())) {
        LOG_ERR("BookCache", "Failed to remove cache subdirectory: %s", currentPath.c_str());
      }
    }

    if (removedCacheData) {
      clearedCount++;
      LOG_DBG("BookCache", "Cleared cache data: %s", cachePath.c_str());
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
