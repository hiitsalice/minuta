#include <BatteryMonitor.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <PowerManager.h>
#include <SPI.h>
#include <XteinkDetect.h>
#include <esp_sleep.h>

// Global HalGPIO instance
HalGPIO gpio;

void HalGPIO::begin() {
#if FREEINK_DEVICE_X4
  BoardConfig::selectDevice(BoardConfig::Board::XteinkX4);

  // Resolve which known X4 controller this production batch uses before SPI
  // claims the display pins.
  freeink::applyXteinkDisplayController();
  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);
  pinMode(BAT_GPIO0, INPUT);
  pinMode(UART0_RXD, INPUT);
#endif
  inputMgr.begin();
}

void HalGPIO::update() {
  inputMgr.update();
  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return inputMgr.wasPressed(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return inputMgr.wasReleased(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

unsigned long HalGPIO::getPowerButtonHeldTime() const { return inputMgr.getPowerButtonHeldTime(); }

bool HalGPIO::hasTouch() const { return inputMgr.hasTouch(); }

bool HalGPIO::hasHomeKey() const { return BoardConfig::hasHomeKey(); }

bool HalGPIO::wasHomeKeyTapped() const { return inputMgr.wasHomeKeyTapped(); }

bool HalGPIO::wasHomeKeyLongPressed() const { return inputMgr.wasHomeKeyLongPressed(); }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const { return inputMgr.wasTouchTap(nx, ny); }

bool HalGPIO::wasTouchDown(float& nx, float& ny) const { return inputMgr.wasTouchPressedAt(nx, ny); }

bool HalGPIO::wasTouchReleased() const { return inputMgr.wasTouchReleased(); }

bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
  return inputMgr.isTouchTapCandidate(nx, ny, heldMs);
}

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const { return inputMgr.isTouchHeldAt(nx, ny); }

bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const { return inputMgr.wasTouchLongPress(nx, ny); }

void HalGPIO::suppressTouchContact() { inputMgr.suppressTouchContact(); }

unsigned long HalGPIO::lastTouchHeldMs() const { return inputMgr.lastTouchHeldMs(); }

bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  return inputMgr.wasSwipe(nxStart, nyStart, nxEnd, nyEnd);
}

bool HalGPIO::wasTouchActivity() const { return inputMgr.wasTouchActivity(); }

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(const bool enabled) {
  InputManager::setSharedConfirmPowerShortPressEmitsPower(enabled);
}

bool HalGPIO::hasEdgeSideButtons() const { return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Pro; }

bool HalGPIO::isXteinkDevice() const { return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4; }

bool HalGPIO::verifyPowerButtonWakeup() {
  // M5Paper v1.1: the classic ESP32's reset-to-setup() latency exceeds a normal
  // wheel click, so a click wake is always released before this samples and
  // verification would re-sleep on every wake. Its wheel has hard external
  // pull-ups, so the ghost-wake debounce this implements is not needed.
  if (BoardConfig::isPaperMono() || BoardConfig::isM5PaperV11() || BoardConfig::ACTIVE.input.power < 0) {
    return true;
  }

  constexpr unsigned long POWER_WAKE_STABILITY_MS = 10;
  const bool heldAtFirstSample = inputMgr.isPowerButtonPhysicallyPressed();
  const unsigned long sampleStart = millis();
  inputMgr.update();
  while (millis() - sampleStart < POWER_WAKE_STABILITY_MS || inputMgr.isDebouncePending()) {
    delay(1);
    inputMgr.update();
  }
  return heldAtFirstSample && inputMgr.isPowerButtonPhysicallyPressed();
}

bool HalGPIO::isUsbConnected() const {
  if (BoardConfig::ACTIVE.usbDetect >= 0) {
    return digitalRead(BoardConfig::ACTIVE.usbDetect) == HIGH;
  }
  // If a board has no digital USB-detect line, infer external power from its
  // charging state instead. BatteryMonitor
  // picks the board's best source — charger IC status, gauge Current() sign, or
  // a /STAT pin — and reports false on boards with no battery telemetry at all.
  // Caveat: charge termination at 100% reads as "not connected".
  static const BatteryMonitor battery;
  return battery.isCharging();
}

bool HalGPIO::coldBootImpliesPowerButton() const {
  // Xteink-style power topology: the power button energizes the rail until
  // firmware latches it, so a no-USB POWERON can only be a still-held button
  // boot, and plugging USB into an off device should charge-sleep, not boot.
  // Everything else boots on any cold boot: boards with no USB detection at
  // all (M5Paper v1.1, PaperColor, Murphy, de-link) would misread USB and
  // post-flash boots as battery button boots, and STAT-only boards like the
  // EEGO A4 misread them the same way once the charger terminates at 100%
  // (STAT inactive reads as "no USB").
  return isXteinkDevice() || BoardConfig::isPaperMono();
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  const bool usbConnected = isUsbConnected();

  if (resetReason == ESP_RST_DEEPSLEEP &&
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected &&
      coldBootImpliesPowerButton()) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
