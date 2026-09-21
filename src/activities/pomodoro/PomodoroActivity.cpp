#include "PomodoroActivity.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "components/UiCountdown.h"
#include "components/UiFocusComplete.h"

namespace fui = freeink::ui;

PomodoroActivity::PomodoroActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Pomodoro", renderer, mappedInput), UiAppHost(renderer) {}

void PomodoroActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_CONTROL, &PomodoroActivity::onControl, this);
  app.on(ACTION_DURATION_SLIDER, &PomodoroActivity::onDurationSlider, this);
  app.on(ACTION_DURATION_STEP, &PomodoroActivity::onDurationStep, this);
  app.setScreen(&PomodoroActivity::timerScreen, this);
  requestUpdate();
}

void PomodoroActivity::onExit() {
  closeRouting();
  app.clearTapFlash();
  Activity::onExit();
}

bool PomodoroActivity::preventAutoSleep() { return timer.isRunning() || timer.isPaused(); }

bool PomodoroActivity::allowsControlCenter() const {
  // A modal overlay suspends this activity's loop and its wake lock.
  return !timer.isRunning() && !timer.isPaused();
}

bool PomodoroActivity::handleHomeGesture() {
  goBack(true);
  requestUpdate(true);  // Home gestures return before the manager's deferred repaint.
  return true;
}

void PomodoroActivity::updateTimer(const uint32_t nowMs) {
  if (timer.tick(nowMs)) {
    view = View::Session;
    selectedControl = 1;
    cleanRefresh = true;
    closeRouting();
    app.clearTapFlash();
    LOG_INF("POMO", "Focus session complete");
    requestUpdate();
  }
  const auto remaining = timer.remainingSeconds(nowMs);
  if ((view == View::Session || view == View::StopPrompt) && displayedSeconds != remaining) {
    displayedSeconds = remaining;
    requestUpdate();
  }
}

void PomodoroActivity::startSession() {
  if (!timer.start(millis(), selectedMinutes * 60U)) {
    LOG_ERR("POMO", "Invalid duration: %u", selectedMinutes);
    return;
  }
  displayedSeconds = selectedMinutes * 60U;
  view = View::Session;
  selectedControl = 0;
  cleanRefresh = true;
  LOG_INF("POMO", "Starting %u minute focus session", selectedMinutes);
}

void PomodoroActivity::selectCustomDuration() {
  draftMinutes = selectedMinutes;
  view = View::Duration;
  selectedControl = 0;
  draggingDuration = false;
  buttonNavigation = false;
  cleanRefresh = true;
}

void PomodoroActivity::setDraftMinutes(const int minutes) {
  RenderLock lock;
  if (view != View::Duration) return;
  const auto clamped = static_cast<uint16_t>(std::clamp(minutes, 1, MAX_MINUTES));
  if (draftMinutes == clamped) return;
  draftMinutes = clamped;
  requestUpdate();
}

void PomodoroActivity::handleDurationButtons() {
  using Button = MappedInputManager::Button;
  static constexpr Button buttons[] = {Button::Left, Button::Right, Button::Up, Button::Down};
  const int sideStep = gpio.hasEdgeSideButtons() ? -5 : 5;
  const int deltas[] = {-1, 1, sideStep, -sideStep};
  const uint32_t now = millis();
  for (int i = 0; i < 4; ++i) {
    if (mappedInput.wasPressed(buttons[i]) ||
        (mappedInput.isPressed(buttons[i]) && mappedInput.getHeldTime() > 500 && now - lastDurationStepAt > 500)) {
      lastDurationStepAt = now;
      setDraftMinutes(draftMinutes + deltas[i]);
      return;
    }
  }
}

void PomodoroActivity::goBack(const bool home) {
  RenderLock lock;
  updateTimer(millis());
  app.clearTapFlash();
  closeRouting();
  if (home && !timer.isRunning() && !timer.isPaused()) {
    lock.unlock();
    onGoHome(HomeMenuItem::APPS);
    return;
  }
  if (!home && view == View::Duration) {
    view = View::Setup;
    selectedControl = 3;
    draggingDuration = false;
  } else if (!home && view == View::StopPrompt) {
    view = View::Session;
    selectedControl = 0;
    exitAfterStop = false;
    exitToHome = false;
  } else if (timer.isRunning() || timer.isPaused()) {
    view = View::StopPrompt;
    selectedControl = 0;
    exitAfterStop = true;
    exitToHome = home;
  } else {
    lock.unlock();
    activityManager.goToApps(AppMenuItem::POMODORO);
    return;
  }
  cleanRefresh = true;
  requestUpdate();
}

void PomodoroActivity::activate(const int control) {
  RenderLock lock;
  // Expiry takes precedence over an input routed from the previous screen.
  const bool wasComplete = timer.isComplete();
  const uint32_t now = millis();
  updateTimer(now);
  if (!wasComplete && timer.isComplete()) return;
  app.clearTapFlash();
  closeRouting();
  if (view == View::Setup) {
    if (control >= 0 && control < 3) {
      selectedMinutes = PRESETS[control];
      displayedSeconds = selectedMinutes * 60U;
      selectedControl = control;
    } else if (control == 3) {
      selectCustomDuration();
    } else if (control == 4) {
      startSession();
    }
  } else if (view == View::Duration) {
    selectedMinutes = draftMinutes;
    displayedSeconds = selectedMinutes * 60U;
    view = View::Setup;
    selectedControl = 4;
    draggingDuration = false;
    cleanRefresh = true;
  } else if (view == View::StopPrompt) {
    if (control == 0) {
      view = View::Session;
      exitAfterStop = false;
      exitToHome = false;
    } else if (control == 1) {
      timer.reset();
      if (exitAfterStop) {
        lock.unlock();
        if (exitToHome) {
          onGoHome(HomeMenuItem::APPS);
        } else {
          activityManager.goToApps(AppMenuItem::POMODORO);
        }
        return;
      }
      view = View::Setup;
      displayedSeconds = selectedMinutes * 60U;
    }
    selectedControl = view == View::Setup ? 4 : 0;
    cleanRefresh = true;
  } else if (timer.isComplete()) {
    if (control == 0) {
      startSession();
    } else if (control == 1) {
      lock.unlock();
      activityManager.goToApps(AppMenuItem::POMODORO);
      return;
    }
  } else if (control == 0) {
    if (timer.isRunning()) {
      timer.pause(now);
    } else {
      timer.resume(now);
    }
    selectedControl = 0;
    updateTimer(millis());
  } else if (control == 1) {
    view = View::StopPrompt;
    exitAfterStop = false;
    exitToHome = false;
    selectedControl = 0;
    cleanRefresh = true;
  }
  requestUpdate();
}

void PomodoroActivity::loop() {
  {
    RenderLock lock;
    const bool wasComplete = timer.isComplete();
    updateTimer(millis());
    if (!wasComplete && timer.isComplete()) return;
  }
  const auto touch = routeTouch(mappedInput, false, view == View::Duration);
  if (touch.routed) {
    RenderLock lock;
    if (buttonNavigation || app.invalidated()) requestUpdate();
    buttonNavigation = false;
  }
  if (touch) {
    if (view == View::Duration && touch.event.dragPermille >= 0) draggingDuration = true;
    return;
  }
  // A slider release can also look like Back; consume the drag's remaining events.
  if (routingReady() && draggingDuration) {
    if (!touch.snap.touchHeld) draggingDuration = false;
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goBack();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(selectedControl);
    return;
  }
  if (view == View::Duration) {
    handleDurationButtons();
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  const bool previous = mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                        swipe == MappedInputManager::SwipeDir::Down;
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                    mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                    swipe == MappedInputManager::SwipeDir::Up;
  if (previous || next) {
    RenderLock lock;
    buttonNavigation = true;
    const int count = view == View::Setup ? 5 : 2;
    selectedControl = (selectedControl + (previous ? count - 1 : 1)) % count;
    requestUpdate();
  }
}

void PomodoroActivity::onControl(const fui::ActionEvent& event, void* user) {
  static_cast<PomodoroActivity*>(user)->activate(event.value);
}

void PomodoroActivity::onDurationSlider(const fui::ActionEvent& event, void* user) {
  if (event.dragPermille < 0) return;
  static_cast<PomodoroActivity*>(user)->setDraftMinutes(1 + (event.dragPermille * (MAX_MINUTES - 1) + 500) / 1000);
}

void PomodoroActivity::onDurationStep(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<PomodoroActivity*>(user);
  self->setDraftMinutes(self->draftMinutes + event.value);
}

void PomodoroActivity::timerScreen(UiScreen& screen, void* user) {
  static_cast<PomodoroActivity*>(user)->buildScreen(screen);
}

void PomodoroActivity::drawControl(UiScreen& screen, const fui::Rect rect, const char* label, const int control,
                                   const bool primary, const bool checked, const fui::ActionId action,
                                   const bool enabled) {
  auto& props = controlProps;
  props.label = label;
  props.action = action;
  props.enabled = enabled;
  props.value = static_cast<int16_t>(control);
  props.inputMask = fui::InputTouch;
  props.state = primary || checked ? fui::StateSelected : fui::StateNormal;
  if (buttonNavigation && action == ACTION_CONTROL && selectedControl == control) props.state = fui::StateFocused;
  props.text = screen.theme().bodyText;
  props.text.bold = true;
  props.styles = screen.theme().button;
  fui::setStyleRadius(props.styles, 0);
  props.minTouchSize = screen.theme().minTouchSize;
  props.radius = 0;
  const auto ink = fui::Paint::solid(screen.theme().bodyText.color);
  if ((view == View::Setup && control < 3) || action == ACTION_DURATION_STEP) {
    props.styles.normal.border = ink;
    props.styles.normal.borderWidth = 1;
  }
  props.styles.focused = props.styles.normal;
  props.styles.focused.border = ink;
  props.styles.focused.borderWidth = 2;
  fui::button(screen.frame(), rect, props);
}

void PomodoroActivity::drawPresets(UiScreen& screen, const fui::Rect rect) {
  const auto& theme = screen.theme();
  const int16_t width = static_cast<int16_t>((rect.width - theme.spaceMd * 2) / 3);
  for (int i = 0; i < 3; ++i) {
    char label[32];
    snprintf(label, sizeof(label), tr(STR_POMODORO_MINUTES), static_cast<unsigned>(PRESETS[i]));
    drawControl(screen,
                fui::Rect{static_cast<int16_t>(rect.x + i * (width + theme.spaceMd)), rect.y, width, rect.height},
                label, i, false, selectedMinutes == PRESETS[i]);
  }
}

void PomodoroActivity::buildScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y), static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  const int16_t side = theme.spaceLg * 2;
  const int16_t end = theme.spaceLg * 3;
  screen.insetContent(fui::Insets{end, side, end, side});
  const int16_t extraWidth = std::max<int16_t>(0, screen.contentRect().width - theme.rowHeight * 6);
  screen.insetContent(fui::Insets{0, static_cast<int16_t>(extraWidth / 2), 0, static_cast<int16_t>(extraWidth / 2)});

  if (view == View::Duration) {
    buildDurationScreen(screen);
    return;
  }
  if (timer.isComplete()) {
    buildCompleteScreen(screen);
    return;
  }

  const char* primary = view == View::Setup        ? tr(STR_POMODORO_FOCUS)
                        : view == View::StopPrompt ? tr(STR_POMODORO_KEEP)
                        : timer.isPaused()         ? tr(STR_RESUME)
                                                   : tr(STR_POMODORO_PAUSE);
  drawControl(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), primary, view == View::Setup ? 4 : 0, true);

  if (view == View::Setup) {
    const bool compact = screen.contentRect().height < theme.rowHeight * 7;
    const int16_t gap = compact ? theme.spaceMd : theme.spaceLg * 3;
    const int16_t clockHeight = std::min<int16_t>(
        theme.rowHeight * 2,
        std::max<int16_t>(0, screen.contentRect().height - theme.rowHeight * 2 - gap - theme.spaceSm));
    const int16_t groupHeight = clockHeight + gap + theme.rowHeight * 2 + theme.spaceSm;
    screen.spacer(std::max<int16_t>(0, (screen.contentRect().height - groupHeight) * 2 / 3));
    drawUiCountdown(screen.target(), screen.takeTop(clockHeight, gap), theme, displayedSeconds);
    drawPresets(screen, screen.takeTop(theme.rowHeight, theme.spaceSm));
    drawControl(screen, screen.takeTop(theme.rowHeight), tr(STR_POMODORO_CUSTOM), 3);
    return;
  }

  drawControl(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_POMODORO_END), 1);

  auto text = theme.bodyText;
  text.align = fui::TextAlign::Center;
  text.bold = true;
  text.maxLines = 2;
  const char* caption = view == View::StopPrompt ? tr(STR_POMODORO_END_QUESTION)
                        : timer.isPaused()       ? tr(STR_POMODORO_PAUSED)
                                                 : nullptr;
  const int16_t captionHeight = screen.target().lineHeight(text.font) * 2;
  const int16_t barHeight = theme.progressHeight * 4;
  const int16_t detailHeight = captionHeight + barHeight + theme.spaceLg * 2;
  const int16_t clockHeight =
      std::min<int16_t>(theme.rowHeight * 2, std::max<int16_t>(0, screen.contentRect().height - detailHeight));
  const int16_t groupHeight = clockHeight + detailHeight;
  screen.spacer(std::max<int16_t>(0, (screen.contentRect().height - groupHeight) / 2));
  drawUiCountdown(screen.target(), screen.takeTop(clockHeight, theme.spaceLg), theme, displayedSeconds);
  drawRemainingProgress(screen, screen.takeTop(barHeight, theme.spaceLg));
  if (caption) {
    screen.target().text(screen.takeTop(captionHeight), caption, text);
  }
}

void PomodoroActivity::drawRemainingProgress(UiScreen& screen, const fui::Rect rect) {
  fui::ProgressBarProps bar;
  bar.value = static_cast<int32_t>(std::min(displayedSeconds, timer.totalSeconds()));
  bar.max = static_cast<int32_t>(std::max<uint32_t>(1, timer.totalSeconds()));
  bar.track = screen.theme().button.normal.background;
  bar.fill = bar.border = fui::Paint::solid(screen.theme().bodyText.color);
  bar.borderWidth = 1;
  bar.radius = 0;
  // Keep one interior column visible until the last second has elapsed.
  bar.minFill = bar.borderWidth + 1;
  fui::progressBar(screen.frame(), rect, bar);
}

void PomodoroActivity::buildCompleteScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  drawControl(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_DONE), 1, true);
  drawControl(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_POMODORO_AGAIN), 0);

  auto heading = theme.titleText;
  heading.align = fui::TextAlign::Center;
  heading.bold = true;
  heading.maxLines = 2;
  auto caption = theme.bodyText;
  caption.align = fui::TextAlign::Center;
  caption.bold = true;
  caption.maxLines = 2;

  const int16_t headingHeight = screen.target().lineHeight(heading.font) * 2;
  const int16_t captionHeight = screen.target().lineHeight(caption.font) * 2;
  const bool compact = screen.contentRect().height < theme.rowHeight * 6;
  const int16_t gap = compact ? theme.spaceSm : theme.spaceLg;
  const int16_t markHeight = compact ? theme.minTouchSize : theme.rowHeight + theme.spaceMd;
  const int16_t fixedHeight = markHeight + headingHeight + captionHeight + gap * 3;
  const int16_t numberHeight =
      std::min<int16_t>(theme.rowHeight * 2, std::max<int16_t>(0, screen.contentRect().height - fixedHeight));
  const int16_t groupHeight = fixedHeight + numberHeight;
  screen.spacer(std::max<int16_t>(0, (screen.contentRect().height - groupHeight) / 2));

  drawUiFocusComplete(screen.target(), screen.takeTop(markHeight, gap), theme);
  screen.target().text(screen.takeTop(headingHeight, gap), tr(STR_POMODORO_COMPLETE), heading);
  const uint32_t minutes = timer.totalSeconds() / 60;
  drawUiMinutes(screen.target(), screen.takeTop(numberHeight, gap), theme, minutes);
  screen.target().text(screen.takeTop(captionHeight),
                       minutes == 1 ? tr(STR_POMODORO_FOCUSED_MINUTE) : tr(STR_POMODORO_FOCUSED_MINUTES), caption);
}

void PomodoroActivity::buildDurationScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  drawControl(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_POMODORO_SET_DURATION), 0, true);

  auto unit = theme.bodyText;
  unit.align = fui::TextAlign::Center;
  unit.bold = true;
  const int16_t unitHeight = screen.target().lineHeight(unit.font);
  const int16_t gap = screen.contentRect().height < theme.rowHeight * 7 ? theme.spaceMd : theme.spaceLg * 2;
  const int16_t fixedHeight = unitHeight + gap + theme.rowHeight * 2 + theme.spaceMd * 2;
  const int16_t numberHeight =
      std::min<int16_t>(theme.rowHeight * 2, std::max<int16_t>(0, screen.contentRect().height - fixedHeight));
  screen.spacer(std::max<int16_t>(0, (screen.contentRect().height - fixedHeight - numberHeight) / 2));
  drawUiMinutes(screen.target(), screen.takeTop(numberHeight, theme.spaceMd), theme, draftMinutes);
  screen.target().text(screen.takeTop(unitHeight, gap), tr(STR_POMODORO_MINUTES_UNIT), unit);

  fui::CapsuleSliderProps slider;
  slider.value = draftMinutes - 1;
  slider.max = MAX_MINUTES - 1;
  slider.action = ACTION_DURATION_SLIDER;
  slider.radius = 0;
  slider.border = slider.fill = fui::Paint::solid(theme.bodyText.color);
  fui::capsuleSlider(screen.frame(), screen.takeTop(theme.rowHeight, theme.spaceMd), slider);

  const auto row = screen.takeTop(theme.rowHeight);
  const int16_t width = (row.width - theme.spaceMd) / 2;
  drawControl(screen, fui::Rect{row.x, row.y, width, row.height}, tr(STR_POMODORO_LESS), -1, false, false,
              ACTION_DURATION_STEP, draftMinutes > 1);
  drawControl(screen, fui::Rect{static_cast<int16_t>(row.right() - width), row.y, width, row.height},
              tr(STR_POMODORO_MORE), 1, false, false, ACTION_DURATION_STEP, draftMinutes < MAX_MINUTES);
}

void PomodoroActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  const auto labels = view == View::Duration ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+")
                                             : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  const uint32_t now = millis();
  // Periodic clean refresh limits accumulated ghosting during long sessions.
  const bool clean = cleanRefresh || now - lastCleanRefreshAt >= 60000;
  renderer.displayBuffer(clean ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  if (clean) lastCleanRefreshAt = now;
  cleanRefresh = false;
}
