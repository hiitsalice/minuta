#ifdef SIMULATOR

#include "OpdsTestGalleryActivity.h"

#include <I18n.h>
#include <Logging.h>

OpdsTestGalleryActivity::OpdsTestGalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("OpdsTestGallery", renderer, mappedInput),
      opdsPreview(renderer, mappedInput,
                  OpdsServer{"Project Gutenberg", "https://www.gutenberg.org/ebooks/search.opds/", "", ""}) {}

void OpdsTestGalleryActivity::onEnter() {
  Activity::onEnter();

  opdsPreview.prepareSimulatorPreview();
  opdsPreview.server.name = "Project Gutenberg";
  opdsPreview.searchTemplate = "https://www.gutenberg.org/ebooks/search.opds/?query={searchTerms}";

  // Representative entries from Project Gutenberg's real public catalogue.
  // The gallery uses the production OPDS renderer; these stable catalogue
  // records keep the simulator preview deterministic and network-independent.
  opdsPreview.entries.clear();
  opdsPreview.entries.reserve(4);
  opdsPreview.entries.push_back(
      OpdsEntry{OpdsEntryType::BOOK, "Pride and Prejudice", "Austen, Jane", "/ebooks/1342.epub.images", ""});
  opdsPreview.entries.push_back(
      OpdsEntry{OpdsEntryType::BOOK, "Dracula", "Stoker, Bram", "/ebooks/345.epub.images", ""});
  opdsPreview.entries.push_back(
      OpdsEntry{OpdsEntryType::BOOK, "Frankenstein", "Shelley, Mary Wollstonecraft", "/ebooks/84.epub.images", ""});
  opdsPreview.entries.push_back(OpdsEntry{OpdsEntryType::BOOK, "The Adventures of Sherlock Holmes",
                                         "Doyle, Arthur Conan", "/ebooks/1661.epub.images", ""});
  opdsPreview.rebuildRowItems();
  opdsPreview.listNav.reset();
  opdsPreview.statusMessage = tr(STR_CHECKING_WIFI);
  opdsPreview.errorMessage = tr(STR_FETCH_FEED_FAILED);
  opdsPreview.downloadProgress = 43;
  opdsPreview.downloadTotal = 100;

  pageIndex = 0;
  page = Page::Status;
  selectPage();
  logCurrentPage();
  requestUpdate();
}

void OpdsTestGalleryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    pageIndex = pageIndex == 0 ? PAGE_COUNT - 1 : pageIndex - 1;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    pageIndex = static_cast<uint8_t>((pageIndex + 1) % PAGE_COUNT);
  } else {
    return;
  }

  page = static_cast<Page>(pageIndex);
  selectPage();
  logCurrentPage();
  requestUpdate();
}

void OpdsTestGalleryActivity::selectPage() {
  switch (page) {
    case Page::Status:
      opdsPreview.state = OpdsBookBrowserActivity::BrowserState::CHECK_WIFI;
      opdsPreview.statusMessage = tr(STR_CHECKING_WIFI);
      break;

    case Page::Browsing:
      opdsPreview.state = OpdsBookBrowserActivity::BrowserState::BROWSING;
      opdsPreview.selectorIndex = 0;
      opdsPreview.listNav.reset();
      break;

    case Page::Downloading:
      opdsPreview.state = OpdsBookBrowserActivity::BrowserState::DOWNLOADING;
      opdsPreview.statusMessage = "Pride and Prejudice";
      opdsPreview.downloadProgress = 43;
      opdsPreview.downloadTotal = 100;
      break;

    case Page::Error:
      opdsPreview.state = OpdsBookBrowserActivity::BrowserState::ERROR;
      opdsPreview.errorMessage = tr(STR_FETCH_FEED_FAILED);
      break;
  }
}

void OpdsTestGalleryActivity::logCurrentPage() const {
  static constexpr const char* PAGE_LABELS[PAGE_COUNT] = {
      "OPDS status", "OPDS browsing", "OPDS downloading", "OPDS error",
  };

  LOG_INF("GALLERY", "Preview %u/%u: %s (side UP/DOWN to navigate)", static_cast<unsigned>(pageIndex + 1),
          static_cast<unsigned>(PAGE_COUNT), PAGE_LABELS[pageIndex]);
}

void OpdsTestGalleryActivity::render(RenderLock&& lock) {
  opdsPreview.render(std::move(lock));
}

#endif
