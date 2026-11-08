#pragma once

#include <cstdint>
#include <functional>

#include "activities/Activity.h"

/**
 * Activity for testing KOReader credentials, or — in sign-up mode — creating a
 * new account on the sync server with the entered username/password.
 * Connects to WiFi, then authenticates or registers.
 */
class KOReaderAuthActivity final : public Activity {
 public:
  enum class Mode { AUTHENTICATE, SIGN_UP, PREVIEW };

  explicit KOReaderAuthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode = Mode::AUTHENTICATE)
      : Activity("KOReaderAuth", renderer, mappedInput), mode(mode) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
#if defined(SIMULATOR)
    if (mode == Mode::PREVIEW) return true;
#endif
    return state == CONNECTING || state == AUTHENTICATING;
  }

 private:
  enum State { WIFI_SELECTION, CONNECTING, AUTHENTICATING, SUCCESS, FAILED };

  Mode mode = Mode::AUTHENTICATE;
  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string errorMessage;

#if defined(SIMULATOR)
  static constexpr uint8_t PREVIEW_SCREEN_COUNT = 6;
  uint8_t previewScreenIndex = 0;
#endif

  void onWifiSelectionComplete(bool success);
  void performAuthentication();
};
