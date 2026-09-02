#include "ButtonRemapActivity.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// UI steps correspond to logical roles in order: Back, Confirm, Left, Right.
constexpr uint8_t kRoleCount = 4;
// Marker used when a role has not been assigned yet.
constexpr uint8_t kUnassigned = 0xFF;
// Duration to show temporary error text when reassigning a button.
constexpr unsigned long kErrorDisplayMs = 1500;
}  // namespace

void ButtonRemapActivity::onEnter() {
  Activity::onEnter();

  // Start with all roles unassigned to avoid duplicate blocking.
  currentStep = 0;
  tempMapping[0] = kUnassigned;
  tempMapping[1] = kUnassigned;
  tempMapping[2] = kUnassigned;
  tempMapping[3] = kUnassigned;
  errorMessage.clear();
  errorUntil = 0;
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    rowItems[i].label = getRoleName(i);
  }
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_12_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  resetUi();
  app.setScreen(&ButtonRemapActivity::screenTrampoline, this);
  requestUpdate();
}

void ButtonRemapActivity::loop() {
  // Clear any temporary warning after its timeout.
  if (errorUntil > 0 && millis() > errorUntil) {
    errorMessage.clear();
    errorUntil = 0;
    requestUpdate();
    return;
  }

  // Side buttons:
  // - Up: restore defaults and restart this remapping sequence.
  // - Down: cancel and return to Settings.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
    SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
    SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
    SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
    SETTINGS.saveToFile();

    currentStep = 0;
    for (uint8_t i = 0; i < kRoleCount; ++i) {
      tempMapping[i] = kUnassigned;
    }
    errorMessage.clear();
    errorUntil = 0;
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    mappedInput.suppressRelease(MappedInputManager::Button::Down);
    finish();
    return;
  }

  {
    // Make sure UI done rendering before accepting another assignment.
    // This avoids rapid double-presses that can advance the step without a visible redraw.
    RenderLock lock(*this);

    // Wait for a front button press to assign to the current role.
    const int pressedButton = mappedInput.getPressedFrontButton();
    if (pressedButton < 0) {
      return;
    }

    // Update temporary mapping and advance the remap step.
    // Only accept the press if this hardware button isn't already assigned elsewhere.
    if (!validateUnassigned(static_cast<uint8_t>(pressedButton))) {
      requestUpdate();
      return;
    }
    tempMapping[currentStep] = static_cast<uint8_t>(pressedButton);
    currentStep++;

    if (currentStep >= kRoleCount) {
      // All roles assigned; save to settings and exit.
      applyTempMapping();
      SETTINGS.saveToFile();
      finish();
      return;
    }

    requestUpdate();
  }
}

void ButtonRemapActivity::render(RenderLock&&) {
  const auto labelForHardware = [&](uint8_t hardwareIndex) -> const char* {
    for (uint8_t i = 0; i < kRoleCount; i++) {
      if (tempMapping[i] == hardwareIndex) {
        return getRoleName(i);
      }
    }
    return "-";
  };

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto drawRemapHelpText =
      [&](const Rect rect, const char* label, const int fontId,
          const EpdFontFamily::Style style) {
        const auto truncated = renderer.truncatedText(
            fontId, label, rect.width - metrics.contentSidePadding * 2, style);
        const int width = renderer.getTextWidth(fontId, truncated.c_str(), style);
        renderer.drawText(fontId, rect.x + (rect.width - width) / 2, rect.y,
                          truncated.c_str(), true, style);
      };

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_REMAP_FRONT_BUTTONS));
  const Rect promptRect{
      0, metrics.topPadding + metrics.headerHeight,
      pageWidth, metrics.tabBarHeight};
  const auto prompt = renderer.truncatedText(
      UI_10_FONT_ID, tr(STR_REMAP_PROMPT),
      pageWidth - metrics.contentSidePadding * 2);
  const int promptY =
      promptRect.y +
      (promptRect.height - renderer.getTextHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(
      UI_10_FONT_ID, metrics.contentSidePadding, promptY, prompt.c_str());
  renderer.drawLine(
      promptRect.x, promptRect.y + promptRect.height - 1,
      promptRect.x + promptRect.width - 1,
      promptRect.y + promptRect.height - 1, true);

  renderUi();

  const int remapRowsBottom =
      metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
      metrics.verticalSpacing + 8 + 4 * metrics.listRowHeight;

  // Temporary warning banner for duplicates.
  if (!errorMessage.empty()) {
    drawRemapHelpText(
                     Rect{0, remapRowsBottom + 24, pageWidth, 20},
                     errorMessage.c_str(), UI_10_FONT_ID, EpdFontFamily::REGULAR);
  }

  // Keep both instructions near the bottom, above the button hints.
  const int instructionDownY =
      pageHeight - metrics.buttonHintsHeight - 24 -
      renderer.getTextHeight(UI_10_FONT_ID);
  const int instructionUpY = instructionDownY - 29;
  drawRemapHelpText(
                   Rect{0, instructionUpY, pageWidth, 20},
                   tr(STR_REMAP_RESET_HINT), UI_10_FONT_ID, EpdFontFamily::REGULAR);
  drawRemapHelpText(
                   Rect{0, instructionDownY, pageWidth, 20},
                   tr(STR_REMAP_CANCEL_HINT), UI_10_FONT_ID, EpdFontFamily::REGULAR);

  // Live preview of logical labels under front buttons.
  // This mirrors the on-device front button order: Back, Confirm, Left, Right.
  GUI.drawButtonHints(renderer, labelForHardware(CrossPointSettings::FRONT_HW_BACK),
                      labelForHardware(CrossPointSettings::FRONT_HW_CONFIRM),
                      labelForHardware(CrossPointSettings::FRONT_HW_LEFT),
                      labelForHardware(CrossPointSettings::FRONT_HW_RIGHT),
                      UI_10_FONT_ID);
  renderer.displayBuffer();
}

void ButtonRemapActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<ButtonRemapActivity*>(user)->buildScreen(screen);
}

void ButtonRemapActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int topOffset = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing + 8;
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(safe.y + topOffset),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width) + metrics.verticalSpacing),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.verticalSpacing),
                  static_cast<int16_t>(safe.x + metrics.verticalSpacing)});

  for (uint8_t i = 0; i < kRoleCount; ++i) {
    const uint8_t assignedButton = tempMapping[i];
    rowItems[i].value = assignedButton == kUnassigned ? tr(STR_UNASSIGNED) : getHardwareName(assignedButton);
  }

  fui::ListProps props;
  props.items = rowItems;
  props.count = kRoleCount;
  props.selectedIndex = currentStep;
  props.inputMask = fui::InputNone;
  props.scrollIndicator = false;
  if (!mappedInput.hasTouch()) {
    props.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  }
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  screen.list(props);
}

void ButtonRemapActivity::applyTempMapping() {
  // Commit temporary mapping into settings (logical role -> hardware).
  SETTINGS.frontButtonBack = tempMapping[0];
  SETTINGS.frontButtonConfirm = tempMapping[1];
  SETTINGS.frontButtonLeft = tempMapping[2];
  SETTINGS.frontButtonRight = tempMapping[3];
}

bool ButtonRemapActivity::validateUnassigned(const uint8_t pressedButton) {
  // Block reusing a hardware button already assigned to another role.
  for (uint8_t i = 0; i < kRoleCount; i++) {
    if (tempMapping[i] == pressedButton && i != currentStep) {
      errorMessage = tr(STR_ALREADY_ASSIGNED);
      errorUntil = millis() + kErrorDisplayMs;
      return false;
    }
  }
  return true;
}

const char* ButtonRemapActivity::getRoleName(const uint8_t roleIndex) const {
  switch (roleIndex) {
    case 0:
      return tr(STR_BACK);
    case 1:
      return tr(STR_CONFIRM);
    case 2:
      return tr(STR_DIR_LEFT);
    case 3:
    default:
      return tr(STR_DIR_RIGHT);
  }
}

const char* ButtonRemapActivity::getHardwareName(const uint8_t buttonIndex) const {
  switch (buttonIndex) {
    case CrossPointSettings::FRONT_HW_BACK:
      return tr(STR_HW_BACK_LABEL);
    case CrossPointSettings::FRONT_HW_CONFIRM:
      return tr(STR_HW_CONFIRM_LABEL);
    case CrossPointSettings::FRONT_HW_LEFT:
      return tr(STR_HW_LEFT_LABEL);
    case CrossPointSettings::FRONT_HW_RIGHT:
      return tr(STR_HW_RIGHT_LABEL);
    default:
      return "Unknown";
  }
}
