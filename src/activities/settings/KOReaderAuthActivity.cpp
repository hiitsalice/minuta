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

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, pageWidth, metrics.headerHeight},
                 mode == Mode::SIGN_UP ? tr(STR_SIGN_UP) : tr(STR_KOREADER_AUTH));

  if (state == AUTHENTICATING) {
    renderer.drawCenteredText(UI_10_FONT_ID, 400, statusMessage.c_str());
  } else if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, 386,
                              mode == Mode::SIGN_UP ? tr(STR_ACCOUNT_CREATED) : tr(STR_AUTH_SUCCESS), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 414, tr(STR_SYNC_READY));
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, 386, mode == Mode::SIGN_UP ? tr(STR_SIGNUP_FAILED) : tr(STR_AUTH_FAILED),
                              true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 414, errorMessage.c_str());
  }

  // Hint strip per state: none while working, Done on success, Back on failure.
  const auto labels = state == SUCCESS  ? mappedInput.mapLabels("", tr(STR_DONE), "", "")
                      : state == FAILED ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                                        : mappedInput.mapLabels("", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void KOReaderAuthActivity::loop() {
  // Done (Confirm) on success, Back on failure; the other button is disabled and ignored.
  if ((state == SUCCESS && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) ||
      (state == FAILED && mappedInput.wasPressed(MappedInputManager::Button::Back))) {
    finish();
  }
}
