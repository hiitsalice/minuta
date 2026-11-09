#pragma once

#ifdef SIMULATOR

#include <cstdint>

#include "activities/Activity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"

class OpdsTestGalleryActivity final : public Activity {
  static constexpr uint8_t PAGE_COUNT = 4;

  enum class Page : uint8_t {
    Status,
    Browsing,
    Downloading,
    Error,
  };

  uint8_t pageIndex = 0;
  Page page = Page::Status;
  OpdsBookBrowserActivity opdsPreview;

  void selectPage();
  void logCurrentPage() const;

 public:
  explicit OpdsTestGalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool preventAutoSleep() override { return true; }
};

#endif
