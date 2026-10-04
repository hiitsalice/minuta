#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

void ClearCacheActivity::onEnter() {
  Activity::onEnter();

  state = WARNING;
  requestUpdate();
}

void ClearCacheActivity::onExit() { Activity::onExit(); }

void ClearCacheActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CLEAR_READING_CACHE));

  if (state == WARNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, 404, "Clear cache?", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 432, "This will clear all reading caches");

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "Clear", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_CLEARING_CACHE));
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    // Fixed layout per audit: header bold at Y=404, result at Y=432.
    renderer.drawCenteredText(UI_10_FONT_ID, 404, tr(STR_CACHE_CLEARED), true, EpdFontFamily::BOLD);
    std::string resultText = std::to_string(clearedCount) + " " + std::string(tr(STR_ITEMS_REMOVED));
    if (failedCount > 0) {
      resultText += ", " + std::to_string(failedCount) + " " + std::string(tr(STR_FAILED_LOWER));
    }
    renderer.drawCenteredText(UI_10_FONT_ID, 432, resultText.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    // Fixed layout per audit: header bold at Y=404, reason at Y=432.
    renderer.drawCenteredText(UI_10_FONT_ID, 404, tr(STR_CLEAR_CACHE_FAILED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 432, tr(STR_CHECK_SERIAL_OUTPUT));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void ClearCacheActivity::beginClear() {
  LOG_DBG("CLEAR_CACHE", "User confirmed, starting cache clear");
  {
    RenderLock lock(*this);
    state = CLEARING;
  }
  requestUpdateAndWait();
  clearCache();
}

void ClearCacheActivity::clearCache() {
  LOG_DBG("CLEAR_CACHE", "Clearing cache...");

  clearedCount = clearAllBookCaches();
  failedCount = 0;

  state = SUCCESS;
  requestUpdate();
}

void ClearCacheActivity::loop() {
  // Handle clear-cache confirmation: button 1 = Cancel, button 2 = Clear.
  // No popup box; see the WARNING render block.
  if (state == WARNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      LOG_DBG("CLEAR_CACHE", "User cancelled");
      goBack();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      beginClear();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}
