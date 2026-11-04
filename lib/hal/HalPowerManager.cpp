#include "HalPowerManager.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <soc/soc_caps.h>

#include <cassert>

#include "HalGPIO.h"

HalPowerManager powerManager;  // Singleton instance

// GPIO13 controls the X4 battery power latch.
static constexpr gpio_num_t XTEINK_C3_GPIO13 = GPIO_NUM_13;
static constexpr uint8_t LOW_BATTERY_CONFIRM_READINGS = 6;

void HalPowerManager::begin() {
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  // Note: We don't use mutex here to avoid too much overhead,
  // it's not very important if we read a slightly stale value for currentLockMode
  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      return;
    }
    isLowPower = true;

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      return;
    }
    isLowPower = false;
  }

  // Otherwise, no change needed
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio) const {
#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  if (gpio.isXteinkDevice()) {
    // GPIO13 gates the battery MOSFET on both Xteink C3 boards; driving it low
    // is the battery power-off (the SDK wake source still handles USB power).
    // Release any surviving pad hold first: hold_en survives deep sleep via
    // the SDK's deepSleep() (esp_sleep_config_gpio_isolate +
    // gpio_deep_sleep_hold_en), and a held pad silently ignores the drive.
    gpio_hold_dis(XTEINK_C3_GPIO13);
    gpio_set_direction(XTEINK_C3_GPIO13, GPIO_MODE_OUTPUT);
    gpio_set_level(XTEINK_C3_GPIO13, 0);
    gpio_hold_en(XTEINK_C3_GPIO13);
  }
#endif

  // Preserve any additional configured power-latch pins during deep sleep.
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0 || static_cast<gpio_num_t>(pin) == XTEINK_C3_GPIO13) continue;
    const auto g = static_cast<gpio_num_t>(pin);
    // Release any surviving pad hold first: a held pad silently ignores the
    // drive below (same trap as the GPIO13 block above).
    gpio_hold_dis(g);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    gpio_hold_en(g);
  }

  // Cut the gated peripheral rails (touch/SD/EPD on boards with switched peripheral power) and
  // hold the enables off through deep sleep — otherwise the GT911 and SD card
  // stay powered all through "off" and drain the battery. No-op on boards with
  // no switched rails (X4). Trade-off: no touch-to-wake; wake is the power
  // button. Must run after display.deepSleep() so the panel controller gets its
  // deep-sleep command while its rail is still up (enterDeepSleep() in main.cpp
  // guarantees that ordering).
  freeink::PowerManager::powerDownRailsForSleep();

  // Waits for the power button to be physically released (so holding it doesn't
  // immediately wake the device again), then arms the wake source and sleeps.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

bool HalPowerManager::isBatteryPercentageKnown() const {
  return _batteryReadingKnown;
}

bool HalPowerManager::isLowBatteryLockoutActive(bool usbConnected) const {
  const unsigned long now = millis();

  // USB is the only way to clear a confirmed lockout.
  if (usbConnected) {
    _lowBatteryZeroReadings = 0;
    _lowBatteryLastConfirmationMs = 0;
    _lowBatteryLockoutActive = false;
    _lowBatteryAwaitingFreshReading = true;
    _lowBatteryLastProcessedSequence = _batterySampleSequence;
    return false;
  }

  // Once locked, ignore all battery percentage changes.
  if (_lowBatteryLockoutActive) {
    return true;
  }

  // After USB is unplugged, require a new physical reading.
  if (_lowBatteryAwaitingFreshReading) {
    if (_batterySampleSequence == _lowBatteryLastProcessedSequence) {
      if (!_batteryHasSample ||
          now - _batteryLastPollMs >= BATTERY_POLL_MS) {
        getBatteryPercentage();
      }
      return false;
    }

    _lowBatteryLastProcessedSequence = _batterySampleSequence;

    if (!_batteryLastReadingValid) {
      return false;
    }

    _lowBatteryAwaitingFreshReading = false;

    const uint16_t percentage =
        BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0
            ? _batteryCachedPercent
            : _batteryCachedPercent / 10;

    if (percentage == 0) {
      _lowBatteryZeroReadings = 1;
      _lowBatteryLastConfirmationMs = now;
    } else {
      _lowBatteryZeroReadings = 0;
      _lowBatteryLastConfirmationMs = 0;
    }

    return false;
  }

  // Process each physical sample only once.
  if (_batterySampleSequence != _lowBatteryLastProcessedSequence) {
    _lowBatteryLastProcessedSequence = _batterySampleSequence;

    // Invalid readings neither count nor reset the sequence.
    if (_batteryLastReadingValid) {
      const uint16_t percentage =
          BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0
              ? _batteryCachedPercent
              : _batteryCachedPercent / 10;

      if (percentage > 0) {
        _lowBatteryZeroReadings = 0;
        _lowBatteryLastConfirmationMs = 0;
      } else {
        if (_lowBatteryZeroReadings < LOW_BATTERY_CONFIRM_READINGS) {
          ++_lowBatteryZeroReadings;
        }
        _lowBatteryLastConfirmationMs = now;
      }
    }
  }

  // Only request additional samples while confirming a zero-percent reading.
  if (_lowBatteryZeroReadings > 0 &&
      _lowBatteryZeroReadings < LOW_BATTERY_CONFIRM_READINGS &&
      now - _lowBatteryLastConfirmationMs >= BATTERY_POLL_MS) {
    getBatteryPercentage();
  }

  if (_lowBatteryZeroReadings >= LOW_BATTERY_CONFIRM_READINGS) {
    _lowBatteryLockoutActive = true;
  }

  return _lowBatteryLockoutActive;
}

uint16_t HalPowerManager::getBatteryPercentage() const {
  const unsigned long now = millis();

  // Reuse the latest sample during the polling interval.
  if (_batteryHasSample &&
      now - _batteryLastPollMs < BATTERY_POLL_MS) {
    return BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0
               ? _batteryCachedPercent
               : _batteryCachedPercent / 10;
  }

  _batteryLastPollMs = now;
  _batteryHasSample = true;
  ++_batterySampleSequence;

  static const BatteryMonitor battery;

  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    uint16_t percent = 0;

    if (!battery.readPercentageChecked(percent)) {
      _batteryLastReadingValid = false;
      return _batteryCachedPercent;
    }

    _batteryCachedPercent = percent;
    _batteryReadingKnown = true;
    _batteryLastReadingValid = true;
    return _batteryCachedPercent;
  }

  // A failed ADC read must not be mistaken for a valid 0% reading.
  const BatteryMonitor::Status status = battery.readStatus();

  if (!status.percentageKnown) {
    _batteryLastReadingValid = false;
    return _batteryCachedPercent / 10;
  }

  _batteryReadingKnown = true;
  _batteryLastReadingValid = true;

  // Smooth the battery percentage, retaining tenths internally.
  if (_batteryCachedPercent == 0) {
    _batteryCachedPercent = 10 * status.percentage;
  } else {
    _batteryCachedPercent =
        (_batteryCachedPercent * 9 + status.percentage * 10) / 10;
  }

  return _batteryCachedPercent / 10;
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
