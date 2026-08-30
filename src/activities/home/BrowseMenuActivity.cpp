#include "BrowseMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId menuItems[BrowseMenuActivity::MENU_ITEM_COUNT] = {
    StrId::STR_LIBRARY,
    StrId::STR_SETTINGS_TITLE,
};
}  // namespace

BrowseMenuActivity::BrowseMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("BrowseMenu", renderer, mappedInput) {
  // Entirely static, so built once here rather than every buildScreen() call.
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[i]);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_[i] = item;
  }
}

int BrowseMenuActivity::listCount() const { return MENU_ITEM_COUNT; }

const char* BrowseMenuActivity::headerTitle() const { return tr(STR_HOME_BROWSE); }

void BrowseMenuActivity::activateIndex(const int index) {
  app.clearTapFlash();
  nav.selected = index;

  if (index == 0) {
    activityManager.goToFileBrowser();
  } else {
    activityManager.goToSettings();
  }
}

void BrowseMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props, /*hasSubtitle=*/false);
  screen.list(props);
}
