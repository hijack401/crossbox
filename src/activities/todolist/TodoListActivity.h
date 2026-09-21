#pragma once

#include <I18n.h>

#include "TodoStore.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

class TodoListActivity final : public Activity, private UiAppHost {
 public:
  TodoListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  bool preventAutoSleep() override { return dirty; }

 private:
  enum class View : uint8_t { List, Selection, Detail, ConfirmDelete, StorageError, ConfirmDiscard, Notice };
  enum Control : int16_t {
    ADD,
    SELECT,
    CANCEL,
    PREVIOUS_PAGE,
    NEXT_PAGE,
    SELECT_ALL,
    DELETE_ALL,
    DELETE_SELECTED,
    EDIT,
    DELETE_ONE,
    CONFIRM_DELETE,
    RETRY,
    DISCARD,
    CONFIRM_DISCARD,
    DISMISS,
    CHECK_BASE = 100,
    ITEM_BASE = 200
  };
  static constexpr freeink::ui::ActionId ACTION_CONTROL = 1;
  static constexpr int MAX_VISIBLE_ROWS = 8;

  // Bounded task storage and render scratch live with the activity, not on its task stack.
  TodoStore store;
  freeink::ui::ButtonProps buttonProps;
  struct FocusTarget {
    int16_t control;
    int16_t y;
    int16_t x;
  };
  FocusTarget focusTargets[24]{};
  int focusCount = 0;
  int selectedControl = ADD;
  int selectedItem = -1;
  int editingItem = -1;
  int page = 0;
  int pageSize = 1;
  uint64_t selection = 0;
  uint64_t deletion = 0;
  View view = View::List;
  View returnView = View::List;
  bool deleteAll = false;
  bool dirty = false;
  bool discardToHome = false;
  bool loadFailed = false;
  bool buttonNavigation = false;
  StrId notice = StrId::STR_TODO_INVALID;

  static void listScreen(UiScreen& screen, void* user);
  static void onControl(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);
  void buildList(UiScreen& screen);
  void buildDetail(UiScreen& screen);
  void buildMessage(UiScreen& screen);
  void drawButton(UiScreen& screen, freeink::ui::Rect rect, const char* label, int control, bool primary = false,
                  bool enabled = true, bool outlined = false);
  void drawTask(UiScreen& screen, freeink::ui::Rect rect, int index);
  void drawTaskText(UiScreen& screen, freeink::ui::Rect rect, const char* text, bool completed, int maxLines = 2);
  void softenCompletedInk(UiScreen& screen, freeink::ui::Rect rect);
  void addFocus(int control, freeink::ui::Rect rect);
  void activate(int control);
  void editTask(int index);
  void acceptText(const ActivityResult& result);
  bool saveChanges();
  void refresh();
  void goBack(bool home = false);
  void changePage(int delta);
  int selectedCount() const;
  uint64_t allItems() const;
};
