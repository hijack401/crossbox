#pragma once

#include "activities/UiListActivity.h"

class AppsActivity final : public UiListActivity {
 public:
  AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, AppMenuItem initialMenuItem);
  void onEnter() override;
  void onExit() override;

 private:
  static constexpr int MENU_ITEM_COUNT = 3;
  const AppMenuItem initialMenuItem;
  freeink::ui::ListItem rowItems[MENU_ITEM_COUNT]{};
  freeink::ui::ListProps listProps;

  int listCount() const override { return MENU_ITEM_COUNT; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
};
