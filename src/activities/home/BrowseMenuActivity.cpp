#include "BrowseMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

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

const char* BrowseMenuActivity::headerTitle() const { return ""; }

void BrowseMenuActivity::drawChrome() {
  // Minuta Browse menu intentionally has no header.
}

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

  // Centre Library + Settings as one block in the usable screen area.
  const int listHeight =
      MENU_ITEM_COUNT * metrics.listRowHeight +
      (MENU_ITEM_COUNT - 1) * metrics.listRowGap;

  const int usableHeight =
      renderer.getScreenHeight() - metrics.buttonHintsHeight;

  const int topInset =
      std::max(0, (usableHeight - listHeight) / 2);

  screen.setContentMargin(
      fui::Insets{
          static_cast<int16_t>(topInset),
          40,
          static_cast<int16_t>(metrics.buttonHintsHeight),
          40});

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;

  props.labelText = screen.theme().bodyText;
  props.labelText.align = fui::TextAlign::Center;

  syncListViewport(screen, props, /*hasSubtitle=*/false);
  screen.list(props);
}
