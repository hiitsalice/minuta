#include "CalibreConnectActivity.h"

#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"

namespace {
constexpr const char* HOSTNAME = "crosspoint";
}  // namespace

void CalibreConnectActivity::onEnter() {
  Activity::onEnter();

  requestUpdate();
  state = CalibreConnectState::WIFI_SELECTION;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;
  lastProgressReceived = 0;
  lastProgressTotal = 0;
  currentUploadName.clear();
  lastCompleteName.clear();
  lastCompleteAt = 0;
  lastProcessedCompleteAt = 0;
  lastErrorName.clear();
  lastErrorAt = 0;
  lastProcessedErrorAt = 0;
  exitRequested = false;

  if (WiFi.status() != WL_CONNECTED) {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& wifi = std::get<WifiResult>(result.data);
                               connectedIP = wifi.ip;
                               connectedSSID = wifi.ssid;
                             }
                             onWifiSelectionComplete(!result.isCancelled);
                           });
  } else {
    connectedIP = WiFi.localIP().toString().c_str();
    connectedSSID = WiFi.SSID().c_str();
    startWebServer();
  }
}

void CalibreConnectActivity::onExit() {
  Activity::onExit();

  MDNS.end();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_MODE_NULL);
  }
}

void CalibreConnectActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    finish();
    return;
  }

  startWebServer();
}

void CalibreConnectActivity::startWebServer() {
  state = CalibreConnectState::SERVER_STARTING;
  requestUpdate();

  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    // mDNS is optional for the Calibre plugin but still helpful for users.
    LOG_DBG("CAL", "mDNS started: http://%s.local/", HOSTNAME);
  }

  // Heap-critical allocation: SD-font caches retained for the CJK UI fallback
  // are rebuildable — release them (again: the WiFi selection screen may have
  // repopulated them rendering a CJK SSID) so the server object doesn't abort
  // on OOM. See CrossPointWebServerActivity::startWebServer().
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("CAL", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("CAL", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
  }

  webServer.reset(new CrossPointWebServer());
  webServer->begin();

  if (webServer->isRunning()) {
    state = CalibreConnectState::SERVER_RUNNING;
    requestUpdate();
  } else {
    state = CalibreConnectState::ERROR;
    requestUpdate();
  }
}

void CalibreConnectActivity::stopWebServer() {
  if (webServer) {
    webServer->stop();
    webServer.reset();
  }
}

void CalibreConnectActivity::loop() {
  // Keep the prior frame's transfer state so a Back press made during a
  // transfer is still ignored if the transfer completes in this loop.
  const bool wasReceivingAtStart = lastProgressTotal > 0;

  if (webServer && webServer->isRunning()) {
    const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;
    if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
      LOG_DBG("CAL", "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
    }

    resetTaskWatchdogIfSubscribed();
    constexpr int MAX_ITERATIONS = 80;
    for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
      webServer->handleClient();
      if ((i & 0x07) == 0x07) {
        resetTaskWatchdogIfSubscribed();
      }
      if ((i & 0x0F) == 0x0F) {
        yield();
      }
    }
    lastHandleClientTime = millis();

    const auto status = webServer->getWsUploadStatus();
    bool changed = false;
    if (status.inProgress) {
      if (status.received != lastProgressReceived || status.total != lastProgressTotal ||
          status.filename != currentUploadName) {
        lastProgressReceived = status.received;
        lastProgressTotal = status.total;
        currentUploadName = status.filename;
        changed = true;
      }
    } else if (lastProgressReceived != 0 || lastProgressTotal != 0) {
      lastProgressReceived = 0;
      lastProgressTotal = 0;
      currentUploadName.clear();
      changed = true;
    }
    // Only update lastCompleteAt if the server has a NEW value (not one we already processed)
    // This prevents restoring an old value after the 6s timeout clears it
    if (status.lastCompleteAt != 0 && status.lastCompleteAt != lastProcessedCompleteAt) {
      lastCompleteAt = status.lastCompleteAt;
      lastCompleteName = status.lastCompleteName;
      lastProcessedCompleteAt = status.lastCompleteAt;  // Mark this value as processed
      if (lastErrorAt > 0 && status.lastCompleteAt > lastErrorAt) {
        lastErrorAt = 0;
        lastErrorName.clear();
      }
      changed = true;
    }
    if (status.lastErrorAt != 0 && status.lastErrorAt != lastProcessedErrorAt &&
        status.lastErrorAt >= status.lastCompleteAt) {
      lastErrorAt = status.lastErrorAt;
      lastErrorName = status.lastErrorName;
      lastProcessedErrorAt = status.lastErrorAt;
      changed = true;
    }
    // A new transfer supersedes an old error message.
    if (status.inProgress && lastErrorAt > 0) {
      lastErrorAt = 0;
      lastErrorName.clear();
      changed = true;
    }
    if (lastCompleteAt > 0 && (millis() - lastCompleteAt) >= 6000) {
      lastCompleteAt = 0;
      lastCompleteName.clear();
      // Note: we DON'T reset lastProcessedCompleteAt here, so we won't re-process the old server value
      changed = true;
    }
    if (lastErrorAt > 0 && (millis() - lastErrorAt) >= 6000) {
      lastErrorAt = 0;
      lastErrorName.clear();
      changed = true;
    }
    if (changed) {
      requestUpdate();
    }
  }

  const auto inputStatus = (webServer && webServer->isRunning())
                                ? webServer->getWsUploadStatus()
                                : CrossPointWebServer::WsUploadStatus{};
  const bool transferActive = wasReceivingAtStart || inputStatus.inProgress || lastProgressTotal > 0;
  const bool transferCompleteVisible = lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000;
  const bool transferErrorVisible = lastErrorAt > 0 && (millis() - lastErrorAt) < 6000 &&
                                     (!transferCompleteVisible || lastErrorAt >= lastCompleteAt);
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (transferActive) {
    // Consume the button event without acting on it while receiving a book.
  } else if (backPressed) {
    if (state == CalibreConnectState::SERVER_RUNNING && transferErrorVisible) {
      // Done dismisses an error but keeps the wireless server available for retry.
      lastErrorAt = 0;
      lastErrorName.clear();
      requestUpdate();
    } else {
      // Back leaves the idle/startup/error screen; Done leaves after success.
      exitRequested = true;
    }
  }
  if (exitRequested) {
    finish();
    return;
  }
}

void CalibreConnectActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, (metrics.topPadding - 6), pageWidth, metrics.headerHeight}, tr(STR_CALIBRE_WIRELESS));

  std::string firstButtonHint;
  if (state == CalibreConnectState::SERVER_STARTING) {
    const int y = (pageHeight - lineHeight) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_STARTING));
    firstButtonHint = tr(STR_BACK);
  } else if (state == CalibreConnectState::ERROR) {
    const int y = (pageHeight - lineHeight) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CONNECTION_FAILED), true, EpdFontFamily::BOLD);
    firstButtonHint = tr(STR_BACK);
  } else if (state == CalibreConnectState::SERVER_RUNNING) {
    const auto uploadStatus = webServer ? webServer->getWsUploadStatus() : CrossPointWebServer::WsUploadStatus{};
    const bool receiving = uploadStatus.inProgress ||
                           (lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal);
    const bool errorIsFresh = lastErrorAt > 0 && (millis() - lastErrorAt) < 6000;
    const bool completionIsFresh = lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000;
    const bool transferErrorVisible = !receiving && errorIsFresh &&
                                      (!completionIsFresh || lastErrorAt >= lastCompleteAt);
    const bool transferCompleteVisible = !receiving && completionIsFresh &&
                                         (!errorIsFresh || lastCompleteAt > lastErrorAt);

    const int rowHeight = metrics.listRowHeight;
    const int contentTop = metrics.topPadding + metrics.headerHeight - 3 +
                           (metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0);
    int y = contentTop + (rowHeight - lineHeight) / 2;
    const int contentWidth = pageWidth - metrics.contentSidePadding * 2;

    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_SETUP), true, EpdFontFamily::BOLD);
    y += rowHeight;

    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_INSTRUCTION_1));
    y += rowHeight;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_INSTRUCTION_2));
    y += rowHeight;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_INSTRUCTION_3));
    y += rowHeight;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_INSTRUCTION_4), true, EpdFontFamily::ITALIC);
    y += rowHeight;

    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_STATUS), true, EpdFontFamily::BOLD);
    y += rowHeight;

    if (receiving) {
      std::string label = tr(STR_CALIBRE_RECEIVING);
      if (!uploadStatus.filename.empty()) label += uploadStatus.filename;
      label = renderer.truncatedText(UI_10_FONT_ID, label.c_str(), contentWidth, EpdFontFamily::REGULAR);
      renderer.drawCenteredText(UI_10_FONT_ID, y, label.c_str());
      if (uploadStatus.total > 0) {
        GUI.drawProgressBar(renderer,
                            Rect{metrics.contentSidePadding, y + lineHeight + metrics.verticalSpacing,
                                 contentWidth, metrics.progressBarHeight},
                            uploadStatus.received, uploadStatus.total);
      }
      firstButtonHint.clear();
    } else if (transferErrorVisible) {
      std::string label = tr(STR_CALIBRE_TRANSFER_FAILED);
      if (!lastErrorName.empty()) label += ": " + lastErrorName;
      label = renderer.truncatedText(UI_10_FONT_ID, label.c_str(), contentWidth, EpdFontFamily::REGULAR);
      renderer.drawCenteredText(UI_10_FONT_ID, y, label.c_str());
      firstButtonHint = tr(STR_DONE);
    } else if (transferCompleteVisible) {
      std::string label = std::string(tr(STR_CALIBRE_RECEIVED)) + lastCompleteName;
      label = renderer.truncatedText(UI_10_FONT_ID, label.c_str(), contentWidth, EpdFontFamily::REGULAR);
      renderer.drawCenteredText(UI_10_FONT_ID, y, label.c_str());
      firstButtonHint = tr(STR_DONE);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_WAITING));
      firstButtonHint = tr(STR_BACK);
    }
  }

  // During a transfer, draw no hint footer at all. Back is consumed and ignored
  // in loop(), and no other buttons have actions on this screen.
  if (!firstButtonHint.empty()) {
    const auto labels = mappedInput.mapLabels(firstButtonHint.c_str(), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
