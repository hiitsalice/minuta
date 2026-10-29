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

#ifndef SIMULATOR
  MDNS.end();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_MODE_NULL);
  }
#endif
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
  const bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);

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
    // Only process a new completion timestamp once, keeping the result on screen
    // until the user dismisses it or another transfer supersedes it.
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
    // A new transfer supersedes previous completion and failure messages.
    if (status.inProgress) {
      if (lastErrorAt > 0) {
        lastErrorAt = 0;
        lastErrorName.clear();
        changed = true;
      }
      if (lastCompleteAt > 0) {
        lastCompleteAt = 0;
        lastCompleteName.clear();
        changed = true;
      }
    }
    if (changed) {
      requestUpdate();
    }
  }

  const auto inputStatus = (webServer && webServer->isRunning())
                                ? webServer->getWsUploadStatus()
                                : CrossPointWebServer::WsUploadStatus{};
  const bool transferActive = wasReceivingAtStart || inputStatus.inProgress || lastProgressTotal > 0;
  const bool completionVisible =
      lastCompleteAt > 0 && (lastErrorAt == 0 || lastCompleteAt > lastErrorAt);

  // Ignore Back and Confirm throughout a real transfer.
  if (!transferActive) {
    if (completionVisible) {
      // Received remains visible until the second button, Done, is pressed.
      if (confirmPressed) {
        exitRequested = true;
      }
    } else if (backPressed) {
      // Back dismisses the waiting or failed screen.
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
  std::string secondButtonHint;
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
    const bool hasError = lastErrorAt > 0;
    const bool hasCompletion = lastCompleteAt > 0;
    const std::string displayFilename = uploadStatus.filename;
    const size_t displayReceived = uploadStatus.received;
    const size_t displayTotal = uploadStatus.total;
    const bool transferErrorVisible = !receiving && hasError &&
                                      (!hasCompletion || lastErrorAt >= lastCompleteAt);
    const bool transferCompleteVisible = !receiving && hasCompletion &&
                                         (!hasError || lastCompleteAt > lastErrorAt);

    const int rowHeight = metrics.listRowHeight;
    const int contentTop = metrics.topPadding + metrics.headerHeight - 3 +
                           (metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0);
    const int contentWidth = pageWidth - metrics.contentSidePadding * 2;
    const int textInset = (rowHeight - lineHeight) / 2 + 1;

    // Ten equal-height rows. Text is 1 px lower within each row.
    int y = contentTop + textInset;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_SETUP),
                              true, EpdFontFamily::BOLD);
    y += rowHeight;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_INSTRUCTION_1));
    y += rowHeight;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_INSTRUCTION_2));
    y += rowHeight;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_INSTRUCTION_3));
    y += rowHeight;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRE_INSTRUCTION_4),
                              true, EpdFontFamily::ITALIC);

    // Rows 6-10: Status, transfer message, filename, progress bar, percentage.
    const int transferTop = contentTop + rowHeight * 5;
    const int statusY = transferTop + textInset;
    const int messageY = transferTop + rowHeight + textInset;
    const int filenameY = transferTop + rowHeight * 2 + textInset;
    const int barY = transferTop + rowHeight * 3 + (rowHeight - 12) / 2 + 1;
    const int percentageY = transferTop + rowHeight * 4 + textInset;

    renderer.drawCenteredText(UI_10_FONT_ID, statusY, tr(STR_CALIBRE_STATUS),
                              true, EpdFontFamily::BOLD);

    std::string message;
    std::string filename;

    if (receiving) {
      message = "Receiving...";
      filename = displayFilename;
    } else if (transferErrorVisible) {
      message = "Transfer Failed";
      filename = lastErrorName;
      firstButtonHint = tr(STR_BACK);
    } else if (transferCompleteVisible) {
      message = "Received!";
      filename = lastCompleteName;
      firstButtonHint.clear();
      secondButtonHint = tr(STR_DONE);
    } else {
      message = tr(STR_CALIBRE_WAITING);
      firstButtonHint = tr(STR_BACK);
    }

    message = renderer.truncatedText(UI_10_FONT_ID, message.c_str(), contentWidth,
                                     EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, messageY, message.c_str());

    if (!filename.empty()) {
      filename = renderer.truncatedText(UI_10_FONT_ID, filename.c_str(), contentWidth,
                                        EpdFontFamily::REGULAR);
      renderer.drawCenteredText(UI_10_FONT_ID, filenameY, filename.c_str());
    }

    if (receiving && displayTotal > 0) {
      // drawProgressBar renders its percentage in the tenth row.
      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, barY,
               pageWidth - metrics.contentSidePadding * 2, 12},
          displayReceived, displayTotal, UI_10_FONT_ID, percentageY);
    }


    if (receiving) {
      firstButtonHint.clear();
    }
  }

  // During a transfer, draw no hint footer at all. Back is consumed and ignored
  // in loop(), and no other buttons have actions on this screen.
  if (!firstButtonHint.empty() || !secondButtonHint.empty()) {
    const auto labels = mappedInput.mapLabels(
        firstButtonHint.c_str(), secondButtonHint.c_str(), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
