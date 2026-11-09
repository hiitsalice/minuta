#pragma once

#ifdef SIMULATOR

#include <cstdint>

#include "activities/Activity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"

class OpdsWifiTestGalleryActivity final : public Activity {
  static constexpr uint8_t PAGE_COUNT = 14;

  enum class Page : uint8_t {
    OpdsStatus,
    OpdsBrowsing,
    OpdsDownloading,
    OpdsError,
    WifiAutoConnecting,
    WifiScanning,
    WifiConnecting,
    WifiNetworkList,
    WifiNoNetworks,
    WifiConnected,
    WifiSavePassword,
    WifiConnectionFailed,
    WifiForgetPassword,
    KeyboardEntry,
  };

  uint8_t pageIndex = 0;
  Page page = Page::OpdsStatus;

  OpdsBookBrowserActivity opdsPreview;
  WifiSelectionActivity wifiPreview;
  KeyboardEntryActivity keyboardPreview;

  void selectPage();
  void logCurrentPage() const;

 public:
  explicit OpdsWifiTestGalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool preventAutoSleep() const override { return true; }
};

#endif
