#ifdef SIMULATOR

#include "OpdsWifiTestGalleryActivity.h"

#include <I18n.h>
#include <Logging.h>

OpdsWifiTestGalleryActivity::OpdsWifiTestGalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("OpdsWifiTestGallery", renderer, mappedInput),
      opdsPreview(renderer, mappedInput, OpdsServer{"Demo Library", "https://demo.invalid/opds", "", ""}),
      wifiPreview(renderer, mappedInput, false),
      keyboardPreview(renderer, mappedInput, tr(STR_ENTER_WIFI_PASSWORD), "", 64, InputType::Text) {}

void OpdsWifiTestGalleryActivity::onEnter() {
  Activity::onEnter();

  opdsPreview.prepareSimulatorPreview();
  wifiPreview.prepareSimulatorPreview();
  keyboardPreview.onEnter();

  opdsPreview.server.name = "Demo Library";
  opdsPreview.searchTemplate = "https://demo.invalid/search?q={searchTerms}";
  opdsPreview.entries.clear();
  opdsPreview.entries.reserve(4);
  opdsPreview.entries.push_back(
      OpdsEntry{OpdsEntryType::BOOK, "The Mysterious Affair at Styles", "Agatha Christie", "/books/styles.epub", ""});
  opdsPreview.entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, "Mystery", "", "/collections/mystery", ""});
  opdsPreview.entries.push_back(
      OpdsEntry{OpdsEntryType::BOOK, "The Murder of Roger Ackroyd", "Agatha Christie", "/books/ackroyd.epub", ""});
  opdsPreview.entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, "Next page", "", "?page=2", ""});
  opdsPreview.rebuildRowItems();
  opdsPreview.listNav.reset();
  opdsPreview.statusMessage = tr(STR_CHECKING_WIFI);
  opdsPreview.errorMessage = tr(STR_FETCH_FEED_FAILED);
  opdsPreview.downloadProgress = 43;
  opdsPreview.downloadTotal = 100;
  opdsPreview.cancelDownload = false;

  wifiPreview.selectedSSID = "Alice Home";
  wifiPreview.connectedIP = "192.168.1.42";
  wifiPreview.connectionError = tr(STR_ERROR_NETWORK_NOT_FOUND);

  pageIndex = 0;
  page = Page::OpdsStatus;
  selectPage();
  logCurrentPage();
  requestUpdate();
}

void OpdsWifiTestGalleryActivity::loop() {
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

void OpdsWifiTestGalleryActivity::selectPage() {
  switch (page) {
    case Page::OpdsStatus:
      opdsPreview.state = OpdsBookBrowserActivity::BrowserState::CHECK_WIFI;
      opdsPreview.statusMessage = tr(STR_CHECKING_WIFI);
      break;

    case Page::OpdsBrowsing:
      opdsPreview.state = OpdsBookBrowserActivity::BrowserState::BROWSING;
      opdsPreview.selectorIndex = 0;
      opdsPreview.listNav.reset();
      break;

    case Page::OpdsDownloading:
      opdsPreview.state = OpdsBookBrowserActivity::BrowserState::DOWNLOADING;
      opdsPreview.statusMessage = "The Mysterious Affair at Styles";
      opdsPreview.downloadProgress = 43;
      opdsPreview.downloadTotal = 100;
      break;

    case Page::OpdsError:
      opdsPreview.state = OpdsBookBrowserActivity::BrowserState::ERROR;
      opdsPreview.errorMessage = tr(STR_FETCH_FEED_FAILED);
      break;

    case Page::WifiAutoConnecting:
      wifiPreview.state = WifiSelectionState::AUTO_CONNECTING;
      wifiPreview.autoConnecting = true;
      wifiPreview.selectedSSID = "Alice Home";
      break;

    case Page::WifiScanning:
      wifiPreview.state = WifiSelectionState::SCANNING;
      wifiPreview.autoConnecting = false;
      break;

    case Page::WifiConnecting:
      wifiPreview.state = WifiSelectionState::CONNECTING;
      wifiPreview.autoConnecting = false;
      wifiPreview.selectedSSID = "Alice Home";
      break;

    case Page::WifiNetworkList:
      wifiPreview.state = WifiSelectionState::NETWORK_LIST;
      wifiPreview.autoConnecting = false;
      wifiPreview.networks.clear();
      wifiPreview.networks.reserve(4);
      wifiPreview.networks.push_back(WifiNetworkInfo{"Alice Home", -42, true, true});
      wifiPreview.networks.push_back(WifiNetworkInfo{"Guest Network", -58, true, false});
      wifiPreview.networks.push_back(WifiNetworkInfo{"Cafe Free Wi-Fi", -68, false, false});
      wifiPreview.appendHiddenNetworkEntry();
      wifiPreview.realNetworkCount = 3;
      wifiPreview.selectedNetworkIndex = 0;
      wifiPreview.rebuildNetworkRowItems();
      break;

    case Page::WifiNoNetworks:
      wifiPreview.state = WifiSelectionState::NETWORK_LIST;
      wifiPreview.autoConnecting = false;
      wifiPreview.networks.clear();
      wifiPreview.networks.reserve(1);
      wifiPreview.appendHiddenNetworkEntry();
      wifiPreview.realNetworkCount = 0;
      wifiPreview.selectedNetworkIndex = 0;
      wifiPreview.rebuildNetworkRowItems();
      break;

    case Page::WifiConnected:
      wifiPreview.state = WifiSelectionState::CONNECTED;
      wifiPreview.selectedSSID = "Alice Home";
      wifiPreview.connectedIP = "192.168.1.42";
      break;

    case Page::WifiSavePassword:
      wifiPreview.state = WifiSelectionState::SAVE_PROMPT;
      wifiPreview.selectedSSID = "Alice Home";
      break;

    case Page::WifiConnectionFailed:
      wifiPreview.state = WifiSelectionState::CONNECTION_FAILED;
      wifiPreview.connectionError = tr(STR_ERROR_NETWORK_NOT_FOUND);
      break;

    case Page::WifiForgetPassword:
      wifiPreview.state = WifiSelectionState::FORGET_PROMPT;
      wifiPreview.selectedSSID = "Alice Home";
      break;

    case Page::KeyboardEntry:
      break;
  }
}

void OpdsWifiTestGalleryActivity::logCurrentPage() const {
  static constexpr const char* PAGE_LABELS[PAGE_COUNT] = {
      "OPDS checking Wi-Fi",   "OPDS book catalogue", "OPDS downloading",    "OPDS error",
      "Wi-Fi auto-connecting", "Wi-Fi scanning",      "Wi-Fi connecting",    "Wi-Fi network list",
      "Wi-Fi no networks",     "Wi-Fi connected",     "Wi-Fi save password", "Wi-Fi connection failed",
      "Wi-Fi forget password", "Keyboard entry",
  };

  LOG_INF("GALLERY", "Preview %u/%u: %s (side UP/DOWN to navigate)", static_cast<unsigned>(pageIndex + 1),
          static_cast<unsigned>(PAGE_COUNT), PAGE_LABELS[pageIndex]);
}

void OpdsWifiTestGalleryActivity::render(RenderLock&& lock) {
  switch (page) {
    case Page::OpdsStatus:
    case Page::OpdsBrowsing:
    case Page::OpdsDownloading:
    case Page::OpdsError:
      opdsPreview.render(std::move(lock));
      return;

    case Page::WifiAutoConnecting:
    case Page::WifiScanning:
    case Page::WifiConnecting:
    case Page::WifiNetworkList:
    case Page::WifiNoNetworks:
    case Page::WifiConnected:
    case Page::WifiSavePassword:
    case Page::WifiConnectionFailed:
    case Page::WifiForgetPassword:
      wifiPreview.render(std::move(lock));
      return;

    case Page::KeyboardEntry:
      keyboardPreview.render(std::move(lock));
      return;
  }
}

#endif
