#include "KOReaderAuthActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderSyncClient.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void KOReaderAuthActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    {
      RenderLock lock(*this);
      state = FAILED;
      errorMessage = tr(STR_WIFI_CONN_FAILED);
    }
    requestUpdate();
    return;
  }

  WiFi.setSleep(false);
  LOG_DBG("KOAuth", "WiFi sleep disabled for authentication");

  {
    RenderLock lock(*this);
    state = AUTHENTICATING;
    statusMessage = mode == Mode::SIGN_UP ? tr(STR_CREATING_ACCOUNT) : tr(STR_AUTHENTICATING);
  }
  requestUpdate();

  performAuthentication();
}

void KOReaderAuthActivity::performAuthentication() {
  const auto result = mode == Mode::SIGN_UP ? KOReaderSyncClient::createUser() : KOReaderSyncClient::authenticate();

  {
    RenderLock lock(*this);
    if (result == KOReaderSyncClient::OK) {
      state = SUCCESS;
      statusMessage = mode == Mode::SIGN_UP ? tr(STR_ACCOUNT_CREATED) : tr(STR_AUTH_SUCCESS);
    } else {
      state = FAILED;
      errorMessage =
          result == KOReaderSyncClient::USER_EXISTS ? tr(STR_USERNAME_TAKEN) : KOReaderSyncClient::errorString(result);
    }
  }
  requestUpdate();
}

void KOReaderAuthActivity::onEnter() {
  Activity::onEnter();

#if defined(SIMULATOR)
  // Static preview mode never starts Wi-Fi or contacts the sync server.
  if (mode == Mode::PREVIEW) {
    previewScreenIndex = 0;
    state = AUTHENTICATING;
    requestUpdate();
    return;
  }
#endif

  // Check if already connected
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }

  // Launch WiFi selection
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void KOReaderAuthActivity::onExit() {
  Activity::onExit();

#if defined(SIMULATOR)
  if (mode == Mode::PREVIEW) return;
#endif

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_MODE_NULL);
  }
}

void KOReaderAuthActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  State screenState = state;
  bool signUp = mode == Mode::SIGN_UP;
  const char* progressMessage = statusMessage.c_str();
  const char* failureMessage = errorMessage.c_str();

#if defined(SIMULATOR)
  if (mode == Mode::PREVIEW) {
    signUp = previewScreenIndex >= 3;
    switch (previewScreenIndex % 3) {
      case 0:
        screenState = AUTHENTICATING;
        progressMessage = tr(signUp ? STR_CREATING_ACCOUNT : STR_AUTHENTICATING);
        break;
      case 1:
        screenState = SUCCESS;
        break;
      default:
        screenState = FAILED;
        failureMessage =
            signUp ? tr(STR_USERNAME_TAKEN) : KOReaderSyncClient::errorString(KOReaderSyncClient::NETWORK_ERROR);
        break;
    }
  }
#endif

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, pageWidth, metrics.headerHeight},
                 signUp ? tr(STR_SIGN_UP) : tr(STR_KOREADER_AUTH));

  // Keep the requested screen positions exact on the X4's 800px portrait canvas.
  const int centreY = static_cast<int>(pageHeight / 2);
  if (screenState == AUTHENTICATING) {
    renderer.drawCenteredText(UI_10_FONT_ID, centreY, progressMessage);
  } else if (screenState == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, centreY - 14,
                              signUp ? tr(STR_ACCOUNT_CREATED) : tr(STR_AUTH_SUCCESS), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centreY + 18, tr(STR_SYNC_READY));
  } else if (screenState == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, centreY - 14, signUp ? tr(STR_SIGNUP_FAILED) : tr(STR_AUTH_FAILED), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centreY + 18, failureMessage);
  }

  // Only the action available in this state is advertised.
  const auto labels = mappedInput.mapLabels(screenState == FAILED ? tr(STR_BACK) : "",
                                            screenState == SUCCESS ? tr(STR_DONE) : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void KOReaderAuthActivity::loop() {
#if defined(SIMULATOR)
  if (mode == Mode::PREVIEW) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      previewScreenIndex = (previewScreenIndex + PREVIEW_SCREEN_COUNT - 1) % PREVIEW_SCREEN_COUNT;
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      previewScreenIndex = (previewScreenIndex + 1) % PREVIEW_SCREEN_COUNT;
      requestUpdate();
    }
    return;
  }
#endif

  // Authentication is deliberately non-interactive while in progress.
  // Success accepts only Confirm/Done; failure accepts only Back.
  if (state == SUCCESS && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finish();
  } else if (state == FAILED && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}
