#include "BrowseMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId menuItems[BrowseMenuActivity::MENU_ITEM_COUNT] = {
    StrId::STR_SETTINGS_TITLE,
    StrId::STR_LIBRARY,
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

const char* BrowseMenuActivity::headerTitle() const { return "Menu"; }

void BrowseMenuActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, renderer.getScreenWidth(), metrics.headerHeight},
                 headerTitle());
}

void BrowseMenuActivity::activateIndex(const int index) {
  app.clearTapFlash();
  nav.selected = index;

  if (index == 0) {
    activityManager.goToSettings();
  } else {
    activityManager.goToFileBrowser();
  }
}

void BrowseMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Same 10pt text as the other list screens.
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  // Centre Settings + Library as one block in the usable screen area.
  // Minuta's two-item Browse menu is deliberately spacious.
  const int browseRowHeight = metrics.listRowHeight;
  const int browseRowGap = 0;
  const int listHeight =
      MENU_ITEM_COUNT * browseRowHeight +
      (MENU_ITEM_COUNT - 1) * browseRowGap;

  (void)listHeight;
  const int topInset = metrics.topPadding + metrics.headerHeight - 3;

  screen.setContentMargin(
      fui::Insets{
          static_cast<int16_t>(topInset),
          0,
          static_cast<int16_t>(metrics.buttonHintsHeight),
          0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  fui::ListProps props;
  props.rowHeight = static_cast<int16_t>(browseRowHeight);
  props.rowGap = static_cast<int16_t>(browseRowGap);
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;

  props.labelText = screen.theme().bodyText;
  props.labelText.align = fui::TextAlign::Center;

  syncListViewport(screen, props, /*hasSubtitle=*/false);
  screen.list(props);
}
