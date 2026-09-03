#pragma once

#include <Arduino.h>
#include <InputManager.h>

// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define SPI_MISO 7  // SPI MISO, shared between SD card and display (Master In Slave Out)

#define BAT_GPIO0 0  // Battery voltage

#define UART0_RXD 20  // Used for USB connection detection

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

  bool lastUsbConnected = false;
  bool usbStateChanged = false;

 public:
  HalGPIO() = default;

  bool isXteinkDevice() const;

  // Start button GPIO and setup SPI for screen and SD card
  void begin();

  // Button input methods
  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  unsigned long getPowerButtonHeldTime() const;
  // The ordinary XTEINK X4 has no touchscreen or capacitive Home key.
  constexpr bool hasTouch() const { return false; }
  constexpr bool hasHomeKey() const { return false; }
  constexpr bool wasHomeKeyTapped() const { return false; }
  constexpr bool wasHomeKeyLongPressed() const { return false; }
  constexpr bool wasTouchTap(float&, float&) const { return false; }
  constexpr bool wasTouchDown(float&, float&) const { return false; }
  constexpr bool wasTouchReleased() const { return false; }
  constexpr bool isTouchTapCandidate(float&, float&, unsigned long&) const { return false; }
  constexpr bool isTouchHeldAt(float&, float&) const { return false; }
  constexpr bool wasTouchLongPress(float&, float&) const { return false; }
  constexpr void suppressTouchContact() {}
  constexpr unsigned long lastTouchHeldMs() const { return 0; }
  constexpr bool wasSwipe(float&, float&, float&, float&) const { return false; }
  constexpr bool wasTouchActivity() const { return false; }
  void setSharedConfirmPowerShortPressEmitsPower(bool enabled);

  // Verify that the physical power button remains held through input debounce.
  // Returns true if verification succeeded, false if device should return to sleep.
  // Should only be called when wakeup reason is PowerButton.
  bool verifyPowerButtonWakeup();

  // Check if USB is connected
  bool isUsbConnected() const;

  // Whether a cold boot with no USB detected can be trusted to mean a held
  // power button (Xteink-style button-energized rail with reliable USB
  // detection). When false, cold boots always proceed to a normal boot.
  bool coldBootImpliesPowerButton() const;

  // Returns true once per edge (plug or unplug) since the last update()
  bool wasUsbStateChanged() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};

extern HalGPIO gpio;
