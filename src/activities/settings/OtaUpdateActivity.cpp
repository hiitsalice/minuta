#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    finish();
    return;
  }

  LOG_DBG("OTA", "WiFi connected, checking for update");

  {
    RenderLock lock(*this);
    state = CHECKING_FOR_UPDATE;
  }
  requestUpdateAndWait();

  const auto res = updater.checkForUpdate();
  // NO_UPDATE here means the release carries no firmware asset for this board
  // (expected until per-board assets are published) — not a failure.
  if (res == OtaUpdater::NO_UPDATE) {
    LOG_DBG("OTA", "No firmware asset for this board in latest release");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    {
      RenderLock lock(*this);
      state = FAILED;
    }
    return;
  }

  if (!updater.isUpdateNewer()) {
    LOG_DBG("OTA", "No new update available");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }

  {
    RenderLock lock(*this);
    state = WAITING_CONFIRMATION;
  }
  const char* options[] = {tr(STR_CANCEL), tr(STR_UPDATE)};
  // Default the selection to Update so the hardware Confirm button installs,
  // matching the pre-popup layout (Back = cancel, Confirm = update).
  confirmPopup.show(tr(STR_NEW_UPDATE), options, 2, 1, [this](const int idx) {
    if (idx == 1) {
      runUpdateInstall();
    } else {
      finish();
    }
  }, true);
  requestUpdate();
}

void OtaUpdateActivity::onEnter() {
  Activity::onEnter();

  // Turn on WiFi immediately
  LOG_DBG("OTA", "Turning on WiFi...");
  WiFi.mode(WIFI_STA);

  // Launch WiFi selection subactivity
  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();

  // Success path reboots via the SHUTTING_DOWN state's plain ESP.restart()
  // (loop() above) so the new firmware boots normally. Back-out paths land
  // here with wifi still active; silent-restart to free the LWIP/mbedTLS
  // fragmentation, same as the other wifi activities.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_MODE_NULL);
  }
}

void OtaUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE));
  const auto height = renderer.getLineHeight(UI_12_FONT_ID);
  constexpr int textGap = 6;
  const auto top = (pageHeight - height) / 2;

  const auto withoutEllipsis = [](const char* value) {
    std::string text = value ? value : "";
    if (text.size() >= 3 && text.compare(text.size() - 3, 3, "...") == 0) {
      text.erase(text.size() - 3);
    }
    return text;
  };

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS) {
    LOG_DBG("OTA", "Update progress: %d / %d", updater.getProcessedSize(), updater.getTotalSize());
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
    // Only update every 2% at the most
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
      return;
    }
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }

  if (state == CHECKING_FOR_UPDATE) {
    // Fixed layout per audit: 10pt, Y=420, keep the "..." (no stripping).
    renderer.drawCenteredText(UI_10_FONT_ID, 420, tr(STR_CHECKING_UPDATE));
  } else if (state == WAITING_CONFIRMATION) {
    // Version info sits in the upper part of the screen so the centered
    // Cancel/Update popup doesn't cover it (same layout as ConfirmationActivity).
    const int headerBottom = metrics.topPadding + metrics.headerHeight;
    const int infoTop = headerBottom + 18;

    renderer.drawText(
        UI_10_FONT_ID,
        metrics.contentSidePadding,
        infoTop,
        (std::string(tr(STR_CURRENT_VERSION)) + CROSSPOINT_VERSION).c_str(),
        true,
        EpdFontFamily::ITALIC);

    renderer.drawText(
        UI_10_FONT_ID,
        metrics.contentSidePadding,
        infoTop + renderer.getLineHeight(UI_10_FONT_ID) + textGap + 6,
        (std::string(tr(STR_NEW_VERSION)) + updater.getLatestVersion()).c_str(),
        true,
        EpdFontFamily::BOLD);

    if (confirmPopup.processRender(renderer, mappedInput)) return;
  } else if (state == UPDATE_IN_PROGRESS) {
    // Keep the progress bar at its existing position.
    const int barY = top + height + textGap;

    // Percentage: 10 pt, 12 px above the bar.
    const int percentHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int percentY = barY - percentHeight - 6;

    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, barY, pageWidth - metrics.contentSidePadding * 2,
             12},
        static_cast<int>(updaterProgress * 100), 100, UI_10_FONT_ID, percentY);

    // Secondary status: centred below the progress bar in 10 pt italic.
    const int statusY = barY + 12 + 12;
    const std::string secondaryText =
        std::to_string(updater.getProcessedSize()) + " / " +
        std::to_string(updater.getTotalSize());

    renderer.drawCenteredText(
        UI_10_FONT_ID, statusY, secondaryText.c_str(),
        true, EpdFontFamily::ITALIC);
  } else if (state == NO_UPDATE) {
    // Fixed layout per audit: Y=420.
    renderer.drawCenteredText(UI_10_FONT_ID, 420, withoutEllipsis(tr(STR_NO_UPDATE)).c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, withoutEllipsis(tr(STR_UPDATE_FAILED)).c_str(), true, EpdFontFamily::BOLD);
    if (failedDetail != nullptr) {
      renderer.drawCenteredText(
          UI_10_FONT_ID,
          top + renderer.getLineHeight(UI_10_FONT_ID) + textGap + 6,
          withoutEllipsis(failedDetail).c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, withoutEllipsis(tr(STR_UPDATE_COMPLETE)).c_str(), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID,
                              top + renderer.getLineHeight(UI_10_FONT_ID) + 9,
                              withoutEllipsis(tr(STR_POWER_ON_HINT)).c_str());
  }

  renderer.displayBuffer();
}

void OtaUpdateActivity::runUpdateInstall() {
  LOG_DBG("OTA", "New update available, starting download...");
  requestUpdateAndWait();
  const auto res = updater.installUpdate(
      [](void* ctx) {
        // immediate=true notifies the render task directly. The default deferred path only
        // sets a flag consumed at the end of ActivityManager::loop(), which never runs while
        // installUpdate() blocks this task.
        static_cast<OtaUpdateActivity*>(ctx)->requestUpdate(true);
      },
      this);

  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update failed: %d", res);
    {
      RenderLock lock(*this);
      failedDetail = res == OtaUpdater::WRONG_DEVICE_ERROR ? tr(STR_FIRMWARE_WRONG_DEVICE) : nullptr;
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = FINISHED;
  }
  requestUpdateAndWait();
  // Hold the completion screen briefly so the user sees it, then restart.
  delay(3000);
  {
    RenderLock lock(*this);
    state = SHUTTING_DOWN;
  }
}

void OtaUpdateActivity::loop() {
  if (state == WAITING_CONFIRMATION) {
    if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    // Popup dismissed without a selection (Back button or tap outside): cancel.
    finish();
    return;
  }

  if (state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == NO_UPDATE) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
