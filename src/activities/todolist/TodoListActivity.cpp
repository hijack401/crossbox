#include "TodoListActivity.h"

#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiFocusComplete.h"

namespace fui = freeink::ui;

TodoListActivity::TodoListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("TodoList", renderer, mappedInput), UiAppHost(renderer) {}

void TodoListActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_CONTROL, &TodoListActivity::onControl, this);
  app.setScreen(&TodoListActivity::listScreen, this);
  loadFailed = !store.load();
  if (loadFailed) {
    view = View::StorageError;
    selectedControl = RETRY;
  }
  requestUpdate();
}

void TodoListActivity::onExit() {
  closeRouting();
  app.clearTapFlash();
  if (dirty && !store.save()) LOG_ERR("TODO", "Unsaved tasks on exit");
  Activity::onExit();
}

bool TodoListActivity::handleHomeGesture() {
  goBack();
  requestUpdate(true);
  return true;
}

void TodoListActivity::refresh() {
  closeRouting();
  app.clearTapFlash();
  requestUpdate();
}

uint64_t TodoListActivity::allItems() const {
  const size_t count = store.model().count();
  return count == TodoListModel::MAX_ITEMS ? UINT64_MAX : (UINT64_C(1) << count) - 1;
}

int TodoListActivity::selectedCount() const {
  int count = 0;
  for (uint64_t bits = selection & allItems(); bits; bits &= bits - 1) ++count;
  return count;
}

bool TodoListActivity::saveChanges() {
  dirty = true;
  if (store.save()) {
    dirty = false;
    return true;
  }
  view = View::StorageError;
  selectedControl = RETRY;
  return false;
}

void TodoListActivity::editTask(const int index) {
  RenderLock lock;
  editingItem = index;
  const auto* item = index >= 0 ? store.model().item(index) : nullptr;
  // A keyboard is allocated only while editing; the shared activity stack owns its lifetime.
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(
      renderer, mappedInput, index < 0 ? tr(STR_TODO_ADD) : tr(STR_TODO_EDIT), item ? item->text : "",
      TodoListModel::MAX_TEXT_BYTES, InputType::Text, true);
  if (!keyboard) {
    LOG_ERR("TODO", "OOM: task keyboard");
    notice = StrId::STR_TODO_UNAVAILABLE;
    returnView = view;
    view = View::Notice;
    selectedControl = DISMISS;
    refresh();
    return;
  }
  closeRouting();
  app.clearTapFlash();
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) { acceptText(result); });
}

void TodoListActivity::acceptText(const ActivityResult& result) {
  RenderLock lock;
  if (!result.isCancelled) {
    const auto* input = std::get_if<KeyboardResult>(&result.data);
    const auto* existing = editingItem >= 0 ? store.model().item(editingItem) : nullptr;
    if (input && existing && std::strcmp(existing->text, input->text.c_str()) == 0) {
      refresh();
      return;
    }
    const size_t newIndex = store.model().openCount();
    if (!input || !(editingItem < 0 ? store.model().add(input->text.c_str())
                                    : store.model().update(editingItem, input->text.c_str()))) {
      returnView = view;
      view = View::Notice;
      notice = StrId::STR_TODO_INVALID;
      selectedControl = DISMISS;
    } else {
      if (editingItem < 0) {
        page = static_cast<int>(newIndex) / std::max(1, pageSize);
        selectedControl = ADD;
      }
      saveChanges();
    }
  }
  refresh();
}

void TodoListActivity::goBack() {
  RenderLock lock;
  if (view == View::ConfirmDelete || view == View::Notice) {
    view = returnView;
    selectedControl = view == View::Detail ? EDIT : view == View::Selection ? SELECT_ALL : ADD;
  } else if (view == View::ConfirmDiscard) {
    view = View::StorageError;
    selectedControl = RETRY;
  } else if (view == View::StorageError && dirty) {
    view = View::ConfirmDiscard;
    selectedControl = CANCEL;
  } else if (view == View::Selection || view == View::Detail) {
    view = View::List;
    selection = 0;
    selectedControl = ADD;
  } else {
    lock.unlock();
    onGoHome(HomeMenuItem::TODO_LIST);
    return;
  }
  refresh();
}

void TodoListActivity::changePage(const int delta) {
  const int last = std::max(0, (static_cast<int>(store.model().count()) - 1) / std::max(1, pageSize));
  page = std::clamp(page + delta, 0, last);
  refresh();
}

void TodoListActivity::activate(const int control) {
  if (control == CANCEL || control == DISMISS) {
    goBack();
    return;
  }
  RenderLock lock;
  if (view == View::StorageError) {
    if (control == RETRY) {
      if (loadFailed) {
        loadFailed = !store.load();
        if (!loadFailed) view = View::List;
      } else if (store.save()) {
        dirty = false;
        view = View::List;
      }
      selectedControl = view == View::List ? ADD : RETRY;
    } else if (control == DISCARD) {
      view = View::ConfirmDiscard;
      selectedControl = CANCEL;
    }
    refresh();
    return;
  }
  if (view == View::ConfirmDiscard) {
    if (control == CONFIRM_DISCARD) {
      dirty = false;
      lock.unlock();
      onGoHome(HomeMenuItem::TODO_LIST);
    }
    return;
  }
  if (view == View::ConfirmDelete) {
    if (control == CONFIRM_DELETE) {
      store.model().removeSelected(deletion);
      selection = 0;
      view = View::List;
      selectedItem = -1;
      selectedControl = ADD;
      saveChanges();
    }
    refresh();
    return;
  }
  if (store.isReadOnly() || view == View::Notice) return;
  if (control >= ITEM_BASE) {
    const int index = control - ITEM_BASE;
    if (!store.model().item(index)) return;
    selectedItem = index;
    view = View::Detail;
    selectedControl = EDIT;
  } else if (control >= CHECK_BASE) {
    const int index = control - CHECK_BASE;
    if (!store.model().item(index)) return;
    if (view == View::Selection) {
      selection ^= UINT64_C(1) << index;
    } else if (store.model().toggle(index)) {
      view = View::List;
      selectedItem = -1;
      saveChanges();
    }
  } else if (control == ADD && view == View::List) {
    if (store.model().count() == TodoListModel::MAX_ITEMS) {
      returnView = view;
      view = View::Notice;
      notice = StrId::STR_TODO_FULL;
      selectedControl = DISMISS;
    } else {
      lock.unlock();
      editTask(-1);
      return;
    }
  } else if (control == EDIT && view == View::Detail) {
    lock.unlock();
    editTask(selectedItem);
    return;
  } else if (control == SELECT && view == View::List && store.model().count()) {
    view = View::Selection;
    selection = 0;
    selectedControl = CHECK_BASE + page * pageSize;
  } else if (control == SELECT_ALL && view == View::Selection) {
    selection = selection == allItems() ? 0 : allItems();
  } else if (control == PREVIOUS_PAGE || control == NEXT_PAGE) {
    changePage(control == PREVIOUS_PAGE ? -1 : 1);
  } else if (control == DELETE_ALL || control == DELETE_SELECTED || control == DELETE_ONE) {
    deletion = control == DELETE_ALL                        ? allItems()
               : control == DELETE_ONE && selectedItem >= 0 ? UINT64_C(1) << selectedItem
                                                            : selection;
    if (deletion) {
      deleteAll = control == DELETE_ALL;
      returnView = view;
      view = View::ConfirmDelete;
      selectedControl = CANCEL;
    }
  }
  refresh();
}

void TodoListActivity::onControl(const fui::ActionEvent& event, void* user) {
  static_cast<TodoListActivity*>(user)->activate(event.value);
}

void TodoListActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed) {
    RenderLock lock;
    if (buttonNavigation || app.invalidated()) requestUpdate();
    buttonNavigation = false;
  }
  if (touch) return;
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    goBack();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    activate(selectedControl);
    return;
  }
  RenderLock lock;
  const auto swipe = mappedInput.wasSwipe();
  if ((view == View::List || view == View::Selection) && swipe != MappedInputManager::SwipeDir::None) {
    if (swipe == MappedInputManager::SwipeDir::Up) changePage(1);
    if (swipe == MappedInputManager::SwipeDir::Down) changePage(-1);
    return;
  }
  const bool previous = mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Left);
  const bool next = mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::Right);
  if ((previous || next) && focusCount) {
    int index = -1;
    for (int i = 0; i < focusCount; ++i)
      if (focusTargets[i].control == selectedControl) index = i;
    index = index < 0 ? (previous ? focusCount - 1 : 0) : (index + (previous ? focusCount - 1 : 1)) % focusCount;
    selectedControl = focusTargets[index].control;
    buttonNavigation = true;
    requestUpdate();
  }
}

void TodoListActivity::addFocus(const int control, const fui::Rect rect) {
  if (focusCount >= 24) return;
  int i = focusCount++;
  while (i > 0 &&
         (focusTargets[i - 1].y > rect.y || (focusTargets[i - 1].y == rect.y && focusTargets[i - 1].x > rect.x))) {
    focusTargets[i] = focusTargets[i - 1];
    --i;
  }
  focusTargets[i] = {static_cast<int16_t>(control), rect.y, rect.x};
}

void TodoListActivity::drawButton(UiScreen& screen, const fui::Rect rect, const char* label, const int control,
                                  const bool primary, const bool enabled, const bool outlined) {
  auto& props = buttonProps;
  props.label = label;
  props.action = ACTION_CONTROL;
  props.value = static_cast<int16_t>(control);
  props.enabled = enabled;
  props.inputMask = fui::InputTouch;
  props.text = screen.theme().bodyText;
  props.text.bold = true;
  props.styles = screen.theme().button;
  fui::setStyleRadius(props.styles, 0);
  props.minTouchSize = screen.theme().minTouchSize;
  props.radius = 0;
  props.state = primary ? fui::StateSelected : fui::StateNormal;
  const auto ink = fui::Paint::solid(screen.theme().bodyText.color);
  if (outlined) {
    props.styles.normal.border = ink;
    props.styles.normal.borderWidth = 1;
  }
  props.styles.focused = props.styles.normal;
  props.styles.focused.border = ink;
  props.styles.focused.borderWidth = 2;
  if (buttonNavigation && selectedControl == control) props.state = fui::StateFocused;
  fui::button(screen.frame(), rect, props);
  if (enabled) addFocus(control, rect);
}

void TodoListActivity::drawTaskText(UiScreen& screen, const fui::Rect rect, const char* text, const bool completed,
                                    const int maxLines) {
  auto style = screen.theme().bodyText;
  style.bold = true;
  style.maxLines = static_cast<uint8_t>(maxLines);
  fui::layoutText(screen.target(), rect, text, style, [&](const char* line, const fui::Rect lineRect) {
    auto single = style;
    single.maxLines = 1;
    screen.target().text(lineRect, line, single);
    if (completed && lineRect.width > 0) {
      const int16_t y = lineRect.y + lineRect.height / 2;
      screen.target().fill(fui::Rect{lineRect.x, y, lineRect.width, 1}, fui::Paint::solid(style.color));
      softenCompletedInk(screen, lineRect);
    }
  });
}

void TodoListActivity::softenCompletedInk(UiScreen& screen, const fui::Rect rect) {
  // Remove one ink pixel per 2x2 cell without another framebuffer or gray-text renderer.
  const auto paper = fui::Paint::solid(fui::invertedColor(screen.theme().bodyText.color));
  for (int16_t y = rect.y + (rect.y & 1); y < rect.bottom(); y += 2) {
    for (int16_t x = rect.x + (rect.x & 1); x < rect.right(); x += 2) {
      screen.target().fill(fui::Rect{x, y, 1, 1}, paper);
    }
  }
}

void TodoListActivity::drawTask(UiScreen& screen, const fui::Rect rect, const int index) {
  const auto* item = store.model().item(index);
  if (!item) return;
  const auto& theme = screen.theme();
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const int16_t touchSize = theme.minTouchSize;
  const int16_t boxSize = touchSize * 2 / 3;
  const fui::Rect check{rect.x, rect.y, touchSize, rect.height};
  const fui::Rect box{static_cast<int16_t>(rect.x + (touchSize - boxSize) / 2),
                      static_cast<int16_t>(rect.y + (rect.height - boxSize) / 2), boxSize, boxSize};
  const bool checked = view == View::Selection ? (selection & (UINT64_C(1) << index)) != 0 : item->completed;
  if (checked) {
    drawUiFocusComplete(screen.target(), box, theme);
    if (item->completed && view != View::Selection) softenCompletedInk(screen, box);
  } else
    screen.target().stroke(box, ink, 2);
  const fui::Rect label{static_cast<int16_t>(check.right() + theme.spaceMd), rect.y,
                        static_cast<int16_t>(rect.right() - check.right() - theme.spaceMd), rect.height};
  drawTaskText(screen, label, item->text, item->completed);
  const int checkControl = CHECK_BASE + index;
  screen.frame().hit(view == View::Selection ? rect : check, ACTION_CONTROL, checkControl, fui::InputTouch);
  addFocus(checkControl, check);
  if (view != View::Selection) {
    screen.frame().hit(label, ACTION_CONTROL, ITEM_BASE + index, fui::InputTouch);
    addFocus(ITEM_BASE + index, label);
  }
  if (buttonNavigation && (selectedControl == checkControl || selectedControl == ITEM_BASE + index)) {
    screen.target().stroke(selectedControl == checkControl && view != View::Selection ? check : rect, ink, 1);
  }
  screen.target().line(fui::Point{rect.x, rect.bottom()}, fui::Point{rect.right(), rect.bottom()}, 1, ink);
}

void TodoListActivity::listScreen(UiScreen& screen, void* user) {
  static_cast<TodoListActivity*>(user)->buildScreen(screen);
}

void TodoListActivity::buildScreen(UiScreen& screen) {
  focusCount = 0;
  const auto& theme = screen.theme();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y), static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  screen.insetContent(fui::Insets{static_cast<int16_t>(theme.spaceLg * 3), static_cast<int16_t>(theme.spaceLg * 2),
                                  static_cast<int16_t>(theme.spaceLg * 3), static_cast<int16_t>(theme.spaceLg * 2)});
  if (view == View::List || view == View::Selection)
    buildList(screen);
  else if (view == View::Detail)
    buildDetail(screen);
  else
    buildMessage(screen);
  bool hasFocus = false;
  for (int i = 0; i < focusCount; ++i) hasFocus |= focusTargets[i].control == selectedControl;
  if (!hasFocus && focusCount) selectedControl = focusTargets[0].control;
}

void TodoListActivity::buildList(UiScreen& screen) {
  const auto& theme = screen.theme();
  const bool selecting = view == View::Selection;
  const auto footer = screen.takeBottom(theme.rowHeight, theme.spaceLg);
  const auto bulk = selecting ? screen.takeBottom(theme.rowHeight, theme.spaceMd) : fui::Rect{};
  const auto header = screen.takeTop(theme.rowHeight, theme.spaceLg);
  const int16_t actionWidth = std::min<int16_t>(header.width / 2, theme.rowHeight * 2);
  char summary[48];
  if (selecting)
    snprintf(summary, sizeof(summary), tr(STR_TODO_SELECTED), selectedCount());
  else
    snprintf(summary, sizeof(summary), tr(STR_TODO_SUMMARY), static_cast<unsigned>(store.model().openCount()),
             static_cast<unsigned>(store.model().count() - store.model().openCount()));
  auto text = theme.bodyText;
  text.bold = true;
  text.maxLines = 2;
  screen.target().text(fui::Rect{header.x, header.y, static_cast<int16_t>(header.width - actionWidth), header.height},
                       summary, text);
  drawButton(screen,
             fui::Rect{static_cast<int16_t>(header.right() - actionWidth), header.y, actionWidth, header.height},
             selecting ? tr(STR_CANCEL) : tr(STR_SELECT), selecting ? CANCEL : SELECT, false,
             selecting || store.model().count() != 0);

  const int count = static_cast<int>(store.model().count());
  const int rowHeight = std::max<int>(theme.rowHeight, screen.target().lineHeight(text.font) * 2 + theme.spaceLg);
  pageSize = std::clamp<int>(screen.contentRect().height / rowHeight, 1, MAX_VISIBLE_ROWS);
  fui::Rect pages{};
  if (count > pageSize) {
    pages = screen.takeBottom(theme.minTouchSize, theme.spaceMd);
    pageSize = std::clamp<int>(screen.contentRect().height / rowHeight, 1, MAX_VISIBLE_ROWS);
  }
  const int pageCount = std::max(1, (count + pageSize - 1) / pageSize);
  page = std::clamp(page, 0, pageCount - 1);
  if (!count) {
    auto empty = theme.titleText;
    empty.align = fui::TextAlign::Center;
    empty.bold = true;
    empty.maxLines = 2;
    screen.target().text(screen.contentRect(), tr(STR_TODO_EMPTY), empty);
  } else {
    const int end = std::min(count, (page + 1) * pageSize);
    for (int i = page * pageSize; i < end; ++i) drawTask(screen, screen.takeTop(rowHeight), i);
  }
  if (!pages.empty()) {
    const int16_t width = pages.width / 3;
    drawButton(screen, fui::Rect{pages.x, pages.y, width, pages.height}, tr(STR_TODO_PREVIOUS), PREVIOUS_PAGE, false,
               page > 0);
    char pageLabel[24];
    snprintf(pageLabel, sizeof(pageLabel), tr(STR_TODO_PAGE), page + 1, pageCount);
    text.align = fui::TextAlign::Center;
    screen.target().text(fui::Rect{static_cast<int16_t>(pages.x + width), pages.y, width, pages.height}, pageLabel,
                         text);
    drawButton(screen, fui::Rect{static_cast<int16_t>(pages.right() - width), pages.y, width, pages.height},
               tr(STR_TODO_NEXT), NEXT_PAGE, false, page + 1 < pageCount);
  }
  if (selecting) {
    const int16_t width = (bulk.width - theme.spaceMd) / 2;
    drawButton(screen, fui::Rect{bulk.x, bulk.y, width, bulk.height},
               selection == allItems() ? tr(STR_TODO_CLEAR_SELECTION) : tr(STR_TODO_SELECT_ALL), SELECT_ALL, false,
               true, true);
    drawButton(screen, fui::Rect{static_cast<int16_t>(bulk.right() - width), bulk.y, width, bulk.height},
               tr(STR_TODO_DELETE_ALL), DELETE_ALL, false, true, true);
    drawButton(screen, footer, tr(STR_TODO_DELETE_SELECTED), DELETE_SELECTED, true, selection != 0);
  } else
    drawButton(screen, footer, tr(STR_TODO_ADD), ADD, true);
}

void TodoListActivity::buildDetail(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto* item = store.model().item(selectedItem);
  if (!item) return;
  drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_TODO_EDIT), EDIT, true);
  drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_TODO_DELETE), DELETE_ONE);
  const auto status = screen.takeBottom(theme.rowHeight, theme.spaceLg);
  const int16_t height = std::min<int16_t>(theme.rowHeight * 3, screen.contentRect().height);
  screen.spacer(std::max<int16_t>(0, (screen.contentRect().height - height) / 2));
  drawTaskText(screen, screen.takeTop(height), item->text, item->completed,
               std::max<int>(1, height / screen.target().lineHeight(theme.bodyText.font)));
  drawButton(screen, status, item->completed ? tr(STR_TODO_REOPEN) : tr(STR_TODO_COMPLETE), CHECK_BASE + selectedItem,
             false, true, true);
}

void TodoListActivity::buildMessage(UiScreen& screen) {
  const auto& theme = screen.theme();
  const char* title;
  const char* body = nullptr;
  char countText[80];
  if (view == View::ConfirmDelete) {
    int count = 0;
    for (uint64_t bits = deletion; bits; bits &= bits - 1) ++count;
    title = deleteAll    ? tr(STR_TODO_DELETE_ALL_QUESTION)
            : count == 1 ? tr(STR_TODO_DELETE_ONE_QUESTION)
                         : tr(STR_TODO_DELETE_QUESTION);
    snprintf(countText, sizeof(countText), tr(STR_TODO_DELETE_COUNT), count);
    body = countText;
    if (count == 1) {
      for (size_t i = 0; i < store.model().count(); ++i) {
        if (deletion & (UINT64_C(1) << i)) body = store.model().item(i)->text;
      }
    }
    drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg),
               deleteAll ? tr(STR_TODO_DELETE_ALL) : tr(STR_TODO_DELETE), CONFIRM_DELETE, true);
    drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_CANCEL), CANCEL);
  } else if (view == View::ConfirmDiscard) {
    title = tr(STR_TODO_DISCARD_QUESTION);
    drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_TODO_DISCARD), CONFIRM_DISCARD, true);
    drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_CANCEL), CANCEL);
  } else if (view == View::StorageError) {
    title = loadFailed ? tr(STR_TODO_LOAD_ERROR) : tr(STR_TODO_SAVE_ERROR);
    body = tr(STR_TODO_CHECK_SD);
    drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_TODO_RETRY), RETRY, true);
    if (dirty) drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_TODO_DISCARD), DISCARD);
  } else {
    title = I18N.get(notice);
    drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_DONE), DISMISS, true);
  }
  auto heading = theme.titleText;
  heading.bold = true;
  heading.align = fui::TextAlign::Center;
  heading.maxLines = 3;
  const int16_t bodyHeight =
      body ? std::min<int16_t>(screen.target().lineHeight(theme.bodyText.font) * 3, screen.contentRect().height / 3)
           : 0;
  const int16_t height = std::min<int16_t>(screen.target().lineHeight(heading.font) * 3,
                                           screen.contentRect().height - bodyHeight - (body ? theme.spaceLg : 0));
  heading.maxLines = std::max<int>(1, height / screen.target().lineHeight(heading.font));
  screen.spacer(
      std::max<int16_t>(0, (screen.contentRect().height - height - bodyHeight - (body ? theme.spaceLg : 0)) / 2));
  screen.target().text(screen.takeTop(height, theme.spaceLg), title, heading);
  if (body) {
    auto caption = theme.bodyText;
    caption.align = fui::TextAlign::Center;
    caption.maxLines = std::max<int>(1, std::min<int>(3, bodyHeight / screen.target().lineHeight(caption.font)));
    screen.target().text(screen.takeTop(bodyHeight), body, caption);
  }
}

void TodoListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
