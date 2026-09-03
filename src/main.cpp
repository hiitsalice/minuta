#include <Arduino.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <WiFi.h>
#include <XteinkDetect.h>
#include <builtinFonts/all.h>
#if FREEINK_CAP_TOUCH
#include <esp_sntp.h>
#endif

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/LoadingIcon.h"
#include "platform/UsbSerialJtagHandoff.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
static unsigned long allowSleepAt = 0;

// A wake hold must never become an in-app power-button action.  Boot may continue
// while the button is held; swallow the one release that ends that wake gesture.
static bool wakePowerReleasePending = false;

// Fonts
EpdFont youngserif14RegularFont(&youngserif_14_regular);
EpdFont youngserif14BoldFont(&youngserif_14_bold);
EpdFont youngserif14ItalicFont(&youngserif_14_italic);
EpdFont youngserif14BoldItalicFont(&youngserif_14_bolditalic);
EpdFontFamily youngserif14FontFamily(&youngserif14RegularFont, &youngserif14BoldFont, &youngserif14ItalicFont,
                                     &youngserif14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont youngserif12RegularFont(&youngserif_12_regular);
EpdFont youngserif12BoldFont(&youngserif_12_bold);
EpdFont youngserif12ItalicFont(&youngserif_12_italic);
EpdFont youngserif12BoldItalicFont(&youngserif_12_bolditalic);
EpdFontFamily youngserif12FontFamily(&youngserif12RegularFont, &youngserif12BoldFont, &youngserif12ItalicFont,
                                     &youngserif12BoldItalicFont);
EpdFont youngserif16RegularFont(&youngserif_16_regular);
EpdFont youngserif16BoldFont(&youngserif_16_bold);
EpdFont youngserif16ItalicFont(&youngserif_16_italic);
EpdFont youngserif16BoldItalicFont(&youngserif_16_bolditalic);
EpdFontFamily youngserif16FontFamily(&youngserif16RegularFont, &youngserif16BoldFont, &youngserif16ItalicFont,
                                     &youngserif16BoldItalicFont);
EpdFont youngserif18RegularFont(&youngserif_18_regular);
EpdFont youngserif18BoldFont(&youngserif_18_bold);
EpdFont youngserif18ItalicFont(&youngserif_18_italic);
EpdFont youngserif18BoldItalicFont(&youngserif_18_bolditalic);
EpdFontFamily youngserif18FontFamily(&youngserif18RegularFont, &youngserif18BoldFont, &youngserif18ItalicFont,
                                     &youngserif18BoldItalicFont);

EpdFont dmsans12RegularFont(&dmsans_12_regular);
EpdFont dmsans12BoldFont(&dmsans_12_bold);
EpdFont dmsans12ItalicFont(&dmsans_12_italic);
EpdFont dmsans12BoldItalicFont(&dmsans_12_bolditalic);
EpdFontFamily dmsans12FontFamily(&dmsans12RegularFont, &dmsans12BoldFont, &dmsans12ItalicFont, &dmsans12BoldItalicFont);
EpdFont dmsans14RegularFont(&dmsans_14_regular);
EpdFont dmsans14BoldFont(&dmsans_14_bold);
EpdFont dmsans14ItalicFont(&dmsans_14_italic);
EpdFont dmsans14BoldItalicFont(&dmsans_14_bolditalic);
EpdFontFamily dmsans14FontFamily(&dmsans14RegularFont, &dmsans14BoldFont, &dmsans14ItalicFont, &dmsans14BoldItalicFont);
EpdFont dmsans16RegularFont(&dmsans_16_regular);
EpdFont dmsans16BoldFont(&dmsans_16_bold);
EpdFont dmsans16ItalicFont(&dmsans_16_italic);
EpdFont dmsans16BoldItalicFont(&dmsans_16_bolditalic);
EpdFontFamily dmsans16FontFamily(&dmsans16RegularFont, &dmsans16BoldFont, &dmsans16ItalicFont, &dmsans16BoldItalicFont);
EpdFont dmsans18RegularFont(&dmsans_18_regular);
EpdFont dmsans18BoldFont(&dmsans_18_bold);
EpdFont dmsans18ItalicFont(&dmsans_18_italic);
EpdFont dmsans18BoldItalicFont(&dmsans_18_bolditalic);
EpdFontFamily dmsans18FontFamily(&dmsans18RegularFont, &dmsans18BoldFont, &dmsans18ItalicFont, &dmsans18BoldItalicFont);

#endif  // OMIT_FONTS

// Dedicated dictionary families with full IPA coverage.
EpdFont andika12RegularFont(&andika_12_regular);
EpdFont andika12BoldFont(&andika_12_bold);
EpdFont andika12ItalicFont(&andika_12_italic);
EpdFont andika14BoldFont(&andika_14_bold);
EpdFontFamily andika12FontFamily(&andika12RegularFont, &andika12BoldFont, &andika12ItalicFont, &andika14BoldFont);

EpdFont andika16BoldFont(&andika_16_bold);
EpdFontFamily andika16HeadwordFontFamily(&andika16BoldFont, &andika16BoldFont, &andika16BoldFont, &andika16BoldFont);

EpdFont steinem8RegularFont(&steinem_8_regular);
EpdFont steinem8BoldFont(&steinem_8_bold);
EpdFont steinem8ItalicFont(&steinem_8_italic);
EpdFont steinem8BoldItalicFont(&steinem_8_bolditalic);
EpdFontFamily steinem8FontFamily(&steinem8RegularFont, &steinem8BoldFont, &steinem8ItalicFont, &steinem8BoldItalicFont);

EpdFont steinem10RegularFont(&steinem_10_regular);
EpdFont steinem10BoldFont(&steinem_10_bold);
EpdFont steinem10ItalicFont(&steinem_10_italic);
EpdFont steinem10BoldItalicFont(&steinem_10_bolditalic);
EpdFontFamily steinem10FontFamily(&steinem10RegularFont, &steinem10BoldFont, &steinem10ItalicFont,
                                  &steinem10BoldItalicFont);

EpdFont steinem11RegularFont(&steinem_11_regular);
EpdFont steinem11BoldFont(&steinem_11_bold);
EpdFont steinem11ItalicFont(&steinem_11_italic);
EpdFont steinem11BoldItalicFont(&steinem_11_bolditalic);
EpdFontFamily steinem11FontFamily(&steinem11RegularFont, &steinem11BoldFont, &steinem11ItalicFont,
                                  &steinem11BoldItalicFont);

EpdFont steinem12RegularFont(&steinem_12_regular);
EpdFont steinem12BoldFont(&steinem_12_bold);
EpdFont steinem12ItalicFont(&steinem_12_italic);
EpdFont steinem12BoldItalicFont(&steinem_12_bolditalic);
EpdFontFamily steinem12FontFamily(&steinem12RegularFont, &steinem12BoldFont, &steinem12ItalicFont,
                                  &steinem12BoldItalicFont);

EpdFont steinem14RegularFont(&steinem_14_regular);
EpdFont steinem14BoldFont(&steinem_14_bold);
EpdFont steinem14ItalicFont(&steinem_14_italic);
EpdFont steinem14BoldItalicFont(&steinem_14_bolditalic);
EpdFontFamily steinem14FontFamily(&steinem14RegularFont, &steinem14BoldFont, &steinem14ItalicFont,
                                  &steinem14BoldItalicFont);

EpdFont steinem18RegularFont(&steinem_18_regular);
EpdFont steinem18BoldFont(&steinem_18_bold);
EpdFont steinem18ItalicFont(&steinem_18_italic);
EpdFont steinem18BoldItalicFont(&steinem_18_bolditalic);
EpdFontFamily steinem18FontFamily(&steinem18RegularFont, &steinem18BoldFont, &steinem18ItalicFont,
                                  &steinem18BoldItalicFont);

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,          // cold boot, flash, panic, or plain reboot
  Silent,          // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  SplashlessWake,  // wake from deep sleep with the splash suppressed by the SD flag
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

#if FREEINK_CAP_TOUCH
static bool finishWifiSessionWithoutRestart() {
  if (!BoardConfig::hasTouch()) return false;

  // A software reset does not cycle externally powered touch rails.
  // Shut down the network stack in place so those peripherals retain state.
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  WiFi.mode(WIFI_OFF);
  delay(100);
  LOG_DBG("MAIN", "WiFi stopped without restart on touch device");
  return true;
}
#endif

void silentRestart() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
#if FREEINK_CAP_TOUCH
  if (finishWifiSessionWithoutRestart()) return;
#endif
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  // E-ink retains the previous frame until Home's first paint lands (~2-3s).
  // Without an overlay, users don't see the reboot and fire input through to
  // Home. Select on the default selectorIndex=0 then opens the most-recent
  // book, looking like a trampoline back to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestartToReader() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
#if FREEINK_CAP_TOUCH
  if (finishWifiSessionWithoutRestart()) return;
#endif
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void restartToHomeAfterStorageHandoff() {
  if (deepSleepInProgress) return;  // sleeping supersedes the storage handoff reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Restart after storage handoff (target=home)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  handoffUsbOtgToSerialJtag();
  ESP.restart();
}

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
}

static bool loadSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  Storage.remove(SLEEP_FRAME_FILE);
  return true;
}

// Enter deep sleep mode
void enterDeepSleep(bool fromTimeout = false) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  const bool isQuickResumeSleep =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  // Every sleep mode leaves a complete retained frame on the e-ink panel. Keep
  // it visible until the first useful reader or home paint replaces it.
  APP_STATE.showBootScreen = false;

  APP_STATE.saveToFile();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);

  if (isQuickResumeSleep) {
    saveSleepFrameBuffer();
  } else if (Storage.exists(SLEEP_FRAME_FILE)) {
    // A stale Quick Resume frame must not replace the selected sleep screen during wake.
    Storage.remove(SLEEP_FRAME_FILE);
  }

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void setupDisplayAndFonts(bool seamless = false) {
  display.begin(seamless);
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(YOUNGSERIF_14_FONT_ID, youngserif14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(YOUNGSERIF_12_FONT_ID, youngserif12FontFamily);
  renderer.insertFont(YOUNGSERIF_16_FONT_ID, youngserif16FontFamily);
  renderer.insertFont(YOUNGSERIF_18_FONT_ID, youngserif18FontFamily);

  renderer.insertFont(DMSANS_12_FONT_ID, dmsans12FontFamily);
  renderer.insertFont(DMSANS_14_FONT_ID, dmsans14FontFamily);
  renderer.insertFont(DMSANS_16_FONT_ID, dmsans16FontFamily);
  renderer.insertFont(DMSANS_18_FONT_ID, dmsans18FontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(DICTIONARY_FONT_ID, andika12FontFamily);
  renderer.insertFont(DICTIONARY_HEADWORD_FONT_ID, andika16HeadwordFontFamily);
  renderer.insertFont(SMALL_FONT_ID, steinem8FontFamily);
  renderer.insertFont(UI_10_FONT_ID, steinem10FontFamily);
  renderer.insertFont(UI_11_FONT_ID, steinem11FontFamily);
  renderer.insertFont(UI_12_FONT_ID, steinem12FontFamily);
  renderer.insertFont(UI_14_FONT_ID, steinem14FontFamily);
  renderer.insertFont(UI_18_FONT_ID, steinem18FontFamily);

  // Discover and load SD card fonts
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
  BoardConfig::holdPowerRails();

#ifdef ENABLE_SERIAL_LOG
#ifdef CROSSPOINT_WAIT_FOR_USB_SERIAL
  // Development builds preserve reliable early CDC logs; release builds let
  // enumeration proceed asynchronously so users do not pay this startup cost.
  delay(250);
#endif
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();
  // checkPanic() clears the watchdog capture marker after a successful SD
  // dump, so retain the boot classification for the later activity route.
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_READER) ? silentRebootTarget : 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;

  gpio.begin();
  powerManager.begin();

  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton && !gpio.verifyPowerButtonWakeup()) {
    LOG_DBG("MAIN", "Power-button wake not held through verification, sleeping");
    powerManager.startDeepSleep(gpio);
  }

  const auto recoveryButton = MappedInputManager::Button::Up;
  const bool recoveryFirmwareMode =
      wakeupReason == HalGPIO::WakeupReason::PowerButton && mappedInputManager.isPressed(recoveryButton);

  LOG_INF("MAIN", "Device: X4");

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  HalSystem::checkPanic();

  APP_STATE.loadFromFile();
  const bool isSleepWake = wakeupReason == HalGPIO::WakeupReason::PowerButton;
  const bool isPersistedSleepWake = isSleepWake && !APP_STATE.showBootScreen;

  if (recoveryFirmwareMode) {
    LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
  }

  // Touch boards default the reader menu to the toolbar overlay instead of the
  // full-screen list. Seeded before the load: fromJson() falls back to the
  // in-memory value only when the file carries no readerMenuStyle key, so a
  // user's saved choice (either style) still wins.
  if (gpio.hasTouch()) {
    SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  }
  SETTINGS.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      wakePowerReleasePending = true;
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // Most devices return to sleep after a USB-powered cold boot.
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  // Only a verified deep-sleep wake may use the one-shot persisted flag.
  // Otherwise a stale flag could suppress the splash on a cold boot.
  const BootResume resume = isSilentReboot         ? BootResume::Silent
                            : isPersistedSleepWake ? BootResume::SplashlessWake
                                                   : BootResume::Splash;
  bool needsWakeRefresh = false;

  setupDisplayAndFonts(resume != BootResume::Splash);

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::SplashlessWake:
      // Minuta always shows its boot screen on a normal hardware boot/wake.
      // Re-arm the flag first so a failed boot cannot leave splash suppressed.
      APP_STATE.showBootScreen = true;
      APP_STATE.saveToFile();
      activityManager.goToBoot();
      break;
    case BootResume::Splash:
      activityManager.goToBoot();
      break;
  }

  // Output polarity is resolved per render by ActivityManager (night mode
  // inverts only the reading surfaces), so nothing to restore here.

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (rebootedFromPanic) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else {
    // Minuta always opens Home after a normal boot or wake.
    activityManager.goHome(HomeMenuItem::NONE, needsWakeRefresh);
  }

  if (resume == BootResume::Silent) {
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

  allowSleepAt = millis() + 2000;
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
  mappedInputManager.update();

  if (activityManager.requiresExclusiveStorageLoop()) {
    // USB Drive handed the raw SD card to the host. Do not run screenshots,
    // sleep, shortcuts, or normal navigation while its filesystem is detached.
    activityManager.loop();
    if (activityManager.preventAutoSleep()) {
      powerManager.setPowerSaving(false);
      delay(10);
    } else {
      // No host is active, so a slower loop is safe. The activity itself times
      // out the raw-storage handoff rather than entering deep sleep detached.
      powerManager.setPowerSaving(true);
      delay(50);
    }
    return;
  }

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity() || activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  // Let wake continue as soon as its hold has been verified. The release can
  // arrive after setup, so consume that one input frame rather than making it
  // a page turn, refresh, or other short power-button action.
  if (wakePowerReleasePending && !gpio.isPressed(HalGPIO::BTN_POWER)) {
    wakePowerReleasePending = false;
    return;
  }

  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (sleepTimeoutMs > 0 && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // A hold that woke the device must be released before it can count as a new
  // in-app long press. Otherwise a user who keeps holding after wake would put
  // the device straight back to sleep once allowSleepAt expires.
  static bool powerReleasedSinceWake = false;
  if (!gpio.isPressed(HalGPIO::BTN_POWER)) powerReleasedSinceWake = true;

  if (powerReleasedSinceWake && millis() >= allowSleepAt && gpio.isPressed(HalGPIO::BTN_POWER) &&
      gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    LOG_DBG("MAIN", "Power button held %lums, sleeping", gpio.getPowerButtonHeldTime());
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
