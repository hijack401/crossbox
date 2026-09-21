#include "AppsActivity.h"

#include <FreeInkUIIcon.h>
#include <I18n.h>

#include <algorithm>

#include "components/UITheme.h"
#include "components/icons/appsListIcons.h"

namespace fui = freeink::ui;

AppsActivity::AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, AppMenuItem initialMenuItem)
    : UiListActivity("Apps", renderer, mappedInput), initialMenuItem(initialMenuItem) {}

void AppsActivity::onEnter() {
  static constexpr StrId LABELS[] = {StrId::STR_TODO_LIST, StrId::STR_POMODORO, StrId::STR_CASINO,
                                     StrId::STR_BREATHWORK};
  for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
    rowItems[i].label = I18N.get(LABELS[i]);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  rowItems[0].icon = fui::bitmapFromIcon(icon_apps_todolist_32);
  rowItems[1].icon = fui::bitmapFromIcon(icon_apps_timer_32);
  rowItems[2].icon = fui::bitmapFromIcon(icon_apps_casino_32);
  rowItems[3].icon = fui::bitmapFromIcon(icon_apps_breathwork_32);
  UiListActivity::onEnter();
  moveSelectionTo(std::clamp(static_cast<int>(initialMenuItem), 0, MENU_ITEM_COUNT - 1));
}

void AppsActivity::onExit() {
  closeRouting();
  app.clearTapFlash();
  Activity::onExit();
}

const char* AppsActivity::headerTitle() const { return tr(STR_APPS); }

void AppsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  listProps.items = rowItems;
  listProps.count = MENU_ITEM_COUNT;
  listProps.action = ACTION_ROW;
  listProps.inputMask = fui::InputTouch;
  listProps.labelText = theme.bodyText;
  listProps.labelText.bold = true;
  listProps.iconSize = 32;
  listProps.rowHeight = theme.rowHeight + theme.spaceLg;
  listProps.rowGap = theme.spaceMd;
  listProps.sidePadding = theme.spaceLg;
  listProps.rowStyles = theme.button;
  listProps.rowStyles.normal.border = fui::Paint::solid(theme.bodyText.color);
  listProps.rowStyles.normal.borderWidth = 1;
  listProps.rowStyles.selected = listProps.rowStyles.normal;
  listProps.rowStyles.selected.borderWidth = 3;
  fui::setStyleRadius(listProps.rowStyles, 0);
  syncListViewport(screen, listProps);
  // Viewport resolution reapplies the theme radius; keep these rows square.
  listProps.rowRadius = 0;
  fui::list(screen.frame(), screen.contentRect(), listProps);
}

void AppsActivity::activateIndex(int index) {
  if (index < 0 || index >= MENU_ITEM_COUNT) return;
  app.clearTapFlash();
  switch (static_cast<AppMenuItem>(index)) {
    case AppMenuItem::TODO_LIST:
      activityManager.goToTodoList();
      break;
    case AppMenuItem::POMODORO:
      activityManager.goToPomodoro();
      break;
    case AppMenuItem::CASINO:
      activityManager.goToCasino();
      break;
    case AppMenuItem::BREATHWORK:
      activityManager.goToBreathwork();
      break;
  }
}

void AppsActivity::onBackButton() {
  app.clearTapFlash();
  activityManager.goHome(HomeMenuItem::APPS);
}
