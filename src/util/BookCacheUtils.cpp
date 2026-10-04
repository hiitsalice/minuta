#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
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
      LOG_DBG("BookCache", "Removing cache: %s", fullPath.c_str());

      file.close();

      if (Storage.removeDir(fullPath.c_str())) {
        clearedCount++;
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
