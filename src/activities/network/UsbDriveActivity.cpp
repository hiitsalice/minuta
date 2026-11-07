#include "UsbDriveActivity.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/UITheme.h"
#include "fontIds.h"


void UsbDriveActivity::onEnter() {
  Activity::onEnter();
#ifdef SIMULATOR
  if (simulatorDemo) {
    preparing = true;
    state = State::WaitingForHost;
    requestUpdate();
    return;
  }
#endif

  // Show the safety instructions before giving the raw SD card to the USB host.
  requestUpdateAndWait();
  if (!Storage.beginUsbDrive()) {
    LOG_ERR("USB", "Unable to start USB Drive");
    preparing = false;
    startFailed = true;
    state = State::IoError;
    startFailureStartedAt = millis();
    requestUpdate();
    return;
  }

  preparing = false;
  state = State::WaitingForHost;
  hostWaitStartedAt = millis();
  requestUpdate();
}

void UsbDriveActivity::onExit() {
#ifdef SIMULATOR
  if (simulatorDemo) {
    Activity::onExit();
    return;
  }
#endif
  if (!restartRequested) Storage.endUsbDrive();
  Activity::onExit();
}

void UsbDriveActivity::loop() {
#ifdef SIMULATOR
  if (simulatorDemo) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Power) ||
        (simulatorDemoState == 1 && mappedInput.wasPressed(MappedInputManager::Button::Back))) {
      onGoHome();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      simulatorDemoState = (simulatorDemoState + 3) % 4;
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      simulatorDemoState = (simulatorDemoState + 1) % 4;
      requestUpdate();
    }
    return;
  }
#endif

  if (!startFailed) {
    const State nextState = Storage.usbDriveState();
    if (nextState != state) {
      state = nextState;
      requestUpdate();
    }
  }

  if (state == State::WaitingForHost && millis() - hostWaitStartedAt >= HOST_WAIT_TIMEOUT_MS) {
    LOG_INF("USB", "USB Drive host wait timed out");
    restartToHome();
    return;
  }

  if (startFailed && millis() - startFailureStartedAt >= START_FAILURE_TIMEOUT_MS) {
    LOG_INF("USB", "USB Drive startup failure timed out");
    restartToHome();
    return;
  }

  if (!startFailed && state == State::IoError) {
    if (!forcedDisconnectRequested) {
      forcedDisconnectRequested = true;
      forcedDisconnectRequestedAt = millis();
      LOG_ERR("USB", "USB Drive I/O error; disconnecting host");
      if (!Storage.disconnectUsbDriveHost()) {
        LOG_ERR("USB", "Unable to request USB Drive host disconnect");
      }
    } else if (millis() - forcedDisconnectRequestedAt >= FORCED_DISCONNECT_TIMEOUT_MS) {
      LOG_ERR("USB", "USB Drive host disconnect timed out; forcing restart");
      restartToHome();
    }
    return;
  }

  if ((state == State::WaitingForHost || startFailed) &&
      (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
       mappedInput.wasPressed(MappedInputManager::Button::Power))) {
    restartToHome();
    return;
  }

  if (state == State::Ejected || state == State::Disconnected || state == State::Unsupported) {
    restartToHome();
  }
}

void UsbDriveActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const Rect headerRect{0, metrics.topPadding - 6, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, headerRect, tr(STR_USB_DRIVE), nullptr);

  const Rect textBounds{0, 0, pageWidth, renderer.getScreenHeight()};
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto drawLine = [&](int centreY, const char* text, EpdFontFamily::Style style) {
    if (text && *text) {
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, centreY - lineHeight / 2, text, true, style);
    }
  };

#ifdef SIMULATOR
  if (simulatorDemo) {
    switch (simulatorDemoState) {
      case 0:
        drawLine(386, tr(STR_USB_DRIVE_PREPARING), EpdFontFamily::BOLD);
        drawLine(414, tr(STR_USB_DRIVE_EJECT_HINT), EpdFontFamily::REGULAR);
        break;
      case 1:
        drawLine(386, tr(STR_USB_DRIVE_WAITING), EpdFontFamily::BOLD);
        drawLine(414, tr(STR_USB_DRIVE_WAITING_HINT), EpdFontFamily::REGULAR);
        break;
      case 2:
        drawLine(372, tr(STR_USB_DRIVE_CONNECTED), EpdFontFamily::BOLD);
        drawLine(400, tr(STR_USB_DRIVE_CONNECT_DELAY), EpdFontFamily::REGULAR);
        drawLine(428, tr(STR_USB_DRIVE_EJECT_HINT), EpdFontFamily::REGULAR);
        break;
      case 3:
        drawLine(386, tr(STR_USB_DRIVE_ERROR), EpdFontFamily::BOLD);
        drawLine(414, tr(STR_USB_DRIVE_START_ERROR), EpdFontFamily::REGULAR);
        break;
    }
  } else
#endif
  if (preparing) {
    drawLine(386, tr(STR_USB_DRIVE_PREPARING), EpdFontFamily::BOLD);
    drawLine(414, tr(STR_USB_DRIVE_EJECT_HINT), EpdFontFamily::REGULAR);
  } else {
    switch (state) {
      case State::WaitingForHost:
        drawLine(386, tr(STR_USB_DRIVE_WAITING), EpdFontFamily::BOLD);
        drawLine(414, tr(STR_USB_DRIVE_WAITING_HINT), EpdFontFamily::REGULAR);
        break;
      case State::Connected:
        drawLine(372, tr(STR_USB_DRIVE_CONNECTED), EpdFontFamily::BOLD);
        drawLine(400, tr(STR_USB_DRIVE_CONNECT_DELAY), EpdFontFamily::REGULAR);
        drawLine(428, tr(STR_USB_DRIVE_EJECT_HINT), EpdFontFamily::REGULAR);
        break;
      case State::IoError:
        drawLine(386, tr(STR_USB_DRIVE_ERROR), EpdFontFamily::BOLD);
        drawLine(414, tr(STR_USB_DRIVE_START_ERROR), EpdFontFamily::REGULAR);
        break;
      case State::Ejected:
      case State::Disconnected:
      case State::Unsupported:
        break;
    }
  }

#ifdef SIMULATOR
  if (simulatorDemo && simulatorDemoState == 1) {
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else
#endif
  if (!preparing && state == State::WaitingForHost && !startFailed) {
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}

void UsbDriveActivity::restartToHome() {
  if (restartRequested) return;
  restartRequested = true;
  Storage.endUsbDrive();
  delay(20);
  restartToHomeAfterStorageHandoff();
}
