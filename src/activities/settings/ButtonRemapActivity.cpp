#include "ButtonRemapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr uint8_t kRoleCount = 4;
constexpr uint8_t kUnassigned = 0xFF;
constexpr int kResetRow = kRoleCount;  // row after the four roles

const char* const kRoleNames[kRoleCount] = {"Back ", "Select ", "Up ", "Down "};
const char* const kButtonNames[kRoleCount] = {"1st Button", "2nd Button", "3rd Button", "4th Button"};

MappedInputManager::Button buttonForRole(const uint8_t role) {
  switch (role) {
    case 0:
      return MappedInputManager::Button::Back;
    case 1:
      return MappedInputManager::Button::Confirm;
    case 2:
      return MappedInputManager::Button::Left;
    default:
      return MappedInputManager::Button::Right;
  }
}
}  // namespace

void ButtonRemapActivity::onEnter() {
  Activity::onEnter();

  // Show the saved mapping when it is a custom one; the default mapping shows as
  // all Unassigned (Done with everything unassigned keeps the default).
  const uint8_t saved[kRoleCount] = {SETTINGS.frontButtonBack, SETTINGS.frontButtonConfirm, SETTINGS.frontButtonLeft,
                                     SETTINGS.frontButtonRight};
  bool valid = true;
  bool seen[kRoleCount] = {false, false, false, false};
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    if (saved[i] >= kRoleCount || seen[saved[i]]) {
      valid = false;
      break;
    }
    seen[saved[i]] = true;
  }
  const bool isDefault = valid && saved[0] == CrossPointSettings::FRONT_HW_BACK &&
                         saved[1] == CrossPointSettings::FRONT_HW_CONFIRM &&
                         saved[2] == CrossPointSettings::FRONT_HW_LEFT && saved[3] == CrossPointSettings::FRONT_HW_RIGHT;
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    tempMapping[i] = (valid && !isDefault) ? saved[i] : kUnassigned;
  }

  selectedRow = 0;
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    rowItems[i].label = kRoleNames[i];
    rowItems[i].labelItalicTail = "(usual)";
  }
  rowItems[kResetRow].label = "Reset Remapping";

  resetUi();
  app.setScreen(&ButtonRemapActivity::screenTrampoline, this);
  requestUpdate();
}

void ButtonRemapActivity::cycleRole(const uint8_t role) {
  // Buttons already taken by the other roles are skipped. Unassigned only ever
  // appears before the first toggle; after that the loop is 1 -> 2 -> 3 -> 4.
  uint8_t used = 0;
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    if (i != role && tempMapping[i] != kUnassigned) {
      used |= static_cast<uint8_t>(1u << tempMapping[i]);
    }
  }
  const uint8_t current = tempMapping[role];
  const uint8_t start = current == kUnassigned ? 0 : static_cast<uint8_t>(current + 1);
  for (uint8_t k = 0; k < kRoleCount; ++k) {
    const uint8_t candidate = static_cast<uint8_t>((start + k) % kRoleCount);
    if ((used & (1u << candidate)) == 0) {
      tempMapping[role] = candidate;
      return;
    }
  }
}

void ButtonRemapActivity::resetAll() {
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    tempMapping[i] = kUnassigned;
  }
}

bool ButtonRemapActivity::tryApplyAndFinish() {
  bool allAssigned = true;
  bool noneAssigned = true;
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    if (tempMapping[i] == kUnassigned) {
      allAssigned = false;
    } else {
      noneAssigned = false;
    }
  }
  // Done only works with a complete mapping, or with none (= default). Otherwise ignore.
  if (!allAssigned && !noneAssigned) {
    return false;
  }

  // The physical button that just pressed Done, under the mapping that is still active.
  const uint8_t doneHardware = SETTINGS.frontButtonBack;

  const uint8_t finalMapping[kRoleCount] = {
      noneAssigned ? static_cast<uint8_t>(CrossPointSettings::FRONT_HW_BACK) : tempMapping[0],
      noneAssigned ? static_cast<uint8_t>(CrossPointSettings::FRONT_HW_CONFIRM) : tempMapping[1],
      noneAssigned ? static_cast<uint8_t>(CrossPointSettings::FRONT_HW_LEFT) : tempMapping[2],
      noneAssigned ? static_cast<uint8_t>(CrossPointSettings::FRONT_HW_RIGHT) : tempMapping[3]};

  SETTINGS.frontButtonBack = finalMapping[0];
  SETTINGS.frontButtonConfirm = finalMapping[1];
  SETTINGS.frontButtonLeft = finalMapping[2];
  SETTINGS.frontButtonRight = finalMapping[3];
  SETTINGS.saveToFile();

  // Under the new mapping the same physical button means a different logical role;
  // swallow its release so it doesn't leak into the Settings screen.
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    if (finalMapping[i] == doneHardware) {
      mappedInput.suppressRelease(buttonForRole(i));
      break;
    }
  }

  finish();
  return true;
}

void ButtonRemapActivity::loop() {
  using Button = MappedInputManager::Button;

  if (mappedInput.wasPressed(Button::Up) || mappedInput.wasPressed(Button::Left)) {
    {
      RenderLock lock(*this);
      selectedRow = selectedRow <= 0 ? kResetRow : selectedRow - 1;
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(Button::Down) || mappedInput.wasPressed(Button::Right)) {
    {
      RenderLock lock(*this);
      selectedRow = selectedRow >= kResetRow ? 0 : selectedRow + 1;
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(Button::Confirm)) {
    {
      RenderLock lock(*this);
      if (selectedRow == kResetRow) {
        resetAll();
      } else {
        cycleRole(static_cast<uint8_t>(selectedRow));
      }
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(Button::Back)) {
    RenderLock lock(*this);
    tryApplyAndFinish();
  }
}

void ButtonRemapActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  // Same header placement as the Customise Status Bar screen.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, pageWidth, metrics.headerHeight},
                 tr(STR_REMAP_FRONT_BUTTONS), nullptr, false, false);

  renderUi();

  bool allAssigned = true;
  bool noneAssigned = true;
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    if (tempMapping[i] == kUnassigned) {
      allAssigned = false;
    } else {
      noneAssigned = false;
    }
  }

  // Show Done only when the mapping can be saved safely.
  const char* doneLabel = (allAssigned || noneAssigned) ? "Done" : "";
  const auto labels = mappedInput.mapLabels(doneLabel, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void ButtonRemapActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<ButtonRemapActivity*>(user)->buildScreen(screen);
}

void ButtonRemapActivity::buildScreen(UiScreen& screen) {
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 3), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  for (uint8_t i = 0; i < kRoleCount; ++i) {
    rowItems[i].value = tempMapping[i] == kUnassigned ? tr(STR_UNASSIGNED) : kButtonNames[tempMapping[i]];
  }
  rowItems[kResetRow].value = nullptr;

  fui::ListProps props;
  props.items = rowItems;
  props.count = kRoleCount + 1;
  props.selectedIndex = static_cast<int16_t>(selectedRow);
  props.inputMask = fui::InputNone;
  props.scrollIndicator = false;
  props.valueInset = 3;
  props.rowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listRowHeight);
  props.rowGap = 0;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  props.labelText.lineGap = 6;
  screen.list(props);
}
