#include "BreathworkActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiBreathwork.h"

namespace fui = freeink::ui;
namespace {
struct Preset {
  StrId name;
  StrId rhythm;
  StrId description;
  StrId instructions;
  uint8_t counts[4];
  uint8_t cycles[3];
};
constexpr Preset PRESETS[] = {
    {StrId::STR_BREATH_EQUAL,
     StrId::STR_BREATH_EQUAL_RHYTHM,
     StrId::STR_BREATH_EQUAL_DESC,
     StrId::STR_BREATH_EQUAL_HELP,
     {5, 0, 5, 0},
     {6, 12, 30}},
    {StrId::STR_BREATH_LONG,
     StrId::STR_BREATH_LONG_RHYTHM,
     StrId::STR_BREATH_LONG_DESC,
     StrId::STR_BREATH_LONG_HELP,
     {4, 0, 6, 0},
     {6, 12, 30}},
    {StrId::STR_BREATH_BOX,
     StrId::STR_BREATH_BOX_RHYTHM,
     StrId::STR_BREATH_BOX_DESC,
     StrId::STR_BREATH_BOX_HELP,
     {4, 4, 4, 4},
     {4, 8, 12}},
    {StrId::STR_BREATH_478,
     StrId::STR_BREATH_478_RHYTHM,
     StrId::STR_BREATH_478_DESC,
     StrId::STR_BREATH_478_HELP,
     {4, 7, 8, 0},
     {2, 3, 4}},
};
constexpr StrId PHASE_NAMES[] = {StrId::STR_BREATH_INHALE, StrId::STR_BREATH_HOLD, StrId::STR_BREATH_EXHALE,
                                 StrId::STR_BREATH_REST};

void timeLabel(char* text, size_t size, uint32_t milliseconds) {
  const uint32_t seconds = (milliseconds + 999) / 1000;
  snprintf(text, size, tr(STR_BREATH_TIME), static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
}
}  // namespace

BreathworkActivity::BreathworkActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Breathwork", renderer, mappedInput), UiAppHost(renderer) {}

void BreathworkActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_CONTROL, &BreathworkActivity::onControl, this);
  app.setScreen(&BreathworkActivity::breathworkScreen, this);
  renderAt = millis();
  requestUpdate();
}

void BreathworkActivity::onExit() {
  session.reset();
  closeRouting();
  app.clearTapFlash();
  Activity::onExit();
}

bool BreathworkActivity::preventAutoSleep() { return view == View::Ready || session.isRunning(); }

bool BreathworkActivity::allowsControlCenter() const { return view != View::Ready && !session.isRunning(); }

bool BreathworkActivity::handleHomeGesture() {
  {
    RenderLock lock;
    session.reset();
    closeRouting();
    app.clearTapFlash();
  }
  onGoHome(HomeMenuItem::APPS);
  return true;
}

uint16_t BreathworkActivity::cycles() const { return PRESETS[preset].cycles[cycleChoice]; }

uint8_t BreathworkActivity::phaseCount(uint8_t phase) const {
  return preset == BOX_PRESET ? boxSeconds : PRESETS[preset].counts[phase];
}

uint32_t BreathworkActivity::durationMs() const {
  uint32_t counts = 0;
  for (uint8_t i = 0; i < 4; ++i) counts += phaseCount(i);
  return counts * countMs * cycles();
}

bool BreathworkActivity::updateSession(uint32_t nowMs) {
  renderAt = nowMs;
  if (view == View::Ready && nowMs - readyAt >= 3000) {
    const uint8_t counts[] = {phaseCount(0), phaseCount(1), phaseCount(2), phaseCount(3)};
    if (!session.start(nowMs, counts, cycles(), countMs)) {
      LOG_ERR("BREATH", "Invalid session configuration");
      view = View::Setup;
      selectedControl = START;
    } else {
      view = View::Session;
      selectedControl = PAUSE;
    }
    closeRouting();
    app.clearTapFlash();
    displayedTick = UINT32_MAX;
    requestUpdate();
    return true;
  }
  if (session.tick(nowMs)) {
    view = View::Complete;
    endedEarly = false;
    selectedControl = DONE;
    cleanRefresh = true;
    closeRouting();
    app.clearTapFlash();
    requestUpdate();
    return true;
  }
  if (view == View::Ready || (view == View::Session && session.isRunning())) {
    const uint32_t stamp = view == View::Ready
                               ? (nowMs - readyAt) / 1000
                               : (session.completedCycles() * 4 + static_cast<uint8_t>(session.phase())) * 32 +
                                     session.remainingCounts(nowMs);
    if (stamp != displayedTick) {
      displayedTick = stamp;
      requestUpdate();
    }
  }
  return false;
}

void BreathworkActivity::finishSession() {
  session.pause(renderAt);
  endedEarly = !session.isComplete();
  view = View::Complete;
  selectedControl = DONE;
  cleanRefresh = true;
}

void BreathworkActivity::goBack() {
  RenderLock lock;
  if (updateSession(millis())) return;
  closeRouting();
  app.clearTapFlash();
  if (view == View::Presets) {
    lock.unlock();
    activityManager.goToApps(AppMenuItem::BREATHWORK);
    return;
  }
  if (view == View::Help) {
    view = helpReturn;
    selectedControl = view == View::Session ? PAUSE : HOW;
  } else if (view == View::Session) {
    finishSession();
  } else if (view == View::Setup) {
    view = View::Presets;
    selectedControl = PRESET_BASE + preset;
  } else {
    session.reset();
    view = View::Setup;
    selectedControl = START;
  }
  cleanRefresh = true;
  requestUpdate();
}

void BreathworkActivity::activate(int16_t control) {
  RenderLock lock;
  if (updateSession(millis())) return;
  if (!routingReady()) return;
  closeRouting();
  app.clearTapFlash();
  if (view == View::Presets && control >= PRESET_BASE && control < PRESET_BASE + 4) {
    preset = control - PRESET_BASE;
    cycleChoice = preset == 3 ? 2 : 1;
    countMs = 1000;
    boxSeconds = DEFAULT_BOX_SECONDS;
    view = View::Setup;
    selectedControl = START;
    cleanRefresh = true;
  } else if (control == HOW && (view == View::Setup || view == View::Session)) {
    session.pause(renderAt);
    helpReturn = view;
    view = View::Help;
    helpPage = 0;
    selectedControl = CLOSE_HELP;
    cleanRefresh = true;
  } else if (view == View::Help && (control == HELP_PREVIOUS || control == HELP_NEXT)) {
    helpPage = (helpPage + (control == HELP_NEXT ? 1 : 3)) % 4;
    selectedControl = control;
    cleanRefresh = true;
  } else if (view == View::Help && control == CLOSE_HELP) {
    view = helpReturn;
    selectedControl = view == View::Session ? PAUSE : HOW;
    cleanRefresh = true;
  } else if ((view == View::Setup && control == START) || (view == View::Complete && control == AGAIN)) {
    session.reset();
    endedEarly = false;
    readyAt = renderAt;
    view = View::Ready;
    selectedControl = CANCEL_READY;
    displayedTick = UINT32_MAX;
    cleanRefresh = true;
  } else if (view == View::Setup && control >= CYCLES_BASE && control < CYCLES_BASE + 3) {
    cycleChoice = control - CYCLES_BASE;
    selectedControl = control;
  } else if (view == View::Setup && preset == BOX_PRESET && (control == BOX_MINUS || control == BOX_PLUS)) {
    boxSeconds = std::clamp<int>(boxSeconds + (control == BOX_PLUS ? 1 : -1), MIN_BOX_SECONDS, MAX_BOX_SECONDS);
    selectedControl = control;
  } else if (view == View::Setup && preset != BOX_PRESET && (control == FASTER || control == STEADY)) {
    countMs = control == FASTER ? 750 : 1000;
    selectedControl = control;
  } else if (view == View::Session && control == PAUSE) {
    if (session.isRunning())
      session.pause(renderAt);
    else
      session.resume(renderAt);
    selectedControl = PAUSE;
    displayedTick = UINT32_MAX;
    cleanRefresh = true;
  } else if (view == View::Session && control == END) {
    finishSession();
  } else if (view == View::Complete && control == DONE) {
    session.reset();
    view = View::Presets;
    selectedControl = PRESET_BASE + preset;
    cleanRefresh = true;
  } else if (view == View::Ready && control == CANCEL_READY) {
    view = View::Setup;
    selectedControl = START;
    cleanRefresh = true;
  }
  requestUpdate();
}

void BreathworkActivity::loop() {
  {
    RenderLock lock;
    if (updateSession(millis())) return;
  }
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
  if (!routingReady()) return;
  if (mappedInput.wasReleased(Button::Confirm)) {
    activate(selectedControl);
    return;
  }
  const bool previous = mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Left);
  const bool next = mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::Right);
  if (previous || next) {
    RenderLock lock;
    if (!focusCount) return;
    int index = -1;
    for (int i = 0; i < focusCount; ++i)
      if (focusTargets[i] == selectedControl) index = i;
    index = index < 0 ? (previous ? focusCount - 1 : 0) : (index + (previous ? focusCount - 1 : 1)) % focusCount;
    selectedControl = focusTargets[index];
    buttonNavigation = true;
    requestUpdate();
  }
}

void BreathworkActivity::onControl(const fui::ActionEvent& event, void* user) {
  static_cast<BreathworkActivity*>(user)->activate(event.value);
}

void BreathworkActivity::breathworkScreen(UiScreen& screen, void* user) {
  static_cast<BreathworkActivity*>(user)->buildScreen(screen);
}

void BreathworkActivity::drawText(UiScreen& screen, fui::Rect rect, const char* label, bool title,
                                  fui::TextAlign align) {
  auto style = title ? screen.theme().titleText : screen.theme().bodyText;
  style.bold = title;
  style.align = align;
  style.maxLines = std::max<int>(1, rect.height / screen.target().lineHeight(style.font));
  screen.target().text(rect, label, style);
}

void BreathworkActivity::drawButton(UiScreen& screen, fui::Rect rect, const char* label, int16_t control, bool selected,
                                    bool outlined) {
  const auto& theme = screen.theme();
  auto& props = buttonProps;
  props.label = label;
  props.action = ACTION_CONTROL;
  props.value = control;
  props.inputMask = fui::InputTouch;
  props.enabled = true;
  props.text = theme.bodyText;
  props.text.bold = true;
  props.styles = theme.button;
  fui::setStyleRadius(props.styles, 0);
  props.radius = 0;
  props.minTouchSize = theme.minTouchSize;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  props.styles.normal.border = ink;
  props.styles.normal.borderWidth = outlined ? 1 : 0;
  props.styles.focused = selected ? props.styles.selected : props.styles.normal;
  props.styles.focused.border = ink;
  props.styles.focused.borderWidth = 2;
  props.state = selected ? fui::StateSelected : fui::StateNormal;
  if (buttonNavigation && selectedControl == control) props.state = fui::StateFocused;
  fui::button(screen.frame(), rect, props);
  if (selected && buttonNavigation && selectedControl == control)
    screen.target().stroke(rect.inset(fui::makeInsets(theme.spaceSm)),
                           fui::Paint::solid(fui::invertedColor(theme.bodyText.color)), 1);
  if (focusCount < 12) focusTargets[focusCount++] = control;
}

void BreathworkActivity::drawHeader(UiScreen& screen, const char* label, bool help) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.titleText.font);
  const auto header = screen.takeTop(std::max<int16_t>(line, theme.minTouchSize), theme.spaceMd);
  auto helpText = theme.bodyText;
  helpText.bold = true;
  const int16_t helpWidth =
      help ? std::max<int16_t>(
                 theme.minTouchSize * 2,
                 screen.target().measureText(helpText.font, tr(STR_BREATH_HOW), helpText).width + theme.spaceMd * 2)
           : 0;
  auto title = theme.titleText;
  title.bold = true;
  if (screen.target().measureText(title.font, label, title).width > header.width - helpWidth - theme.spaceSm)
    title.font = theme.bodyText.font;
  screen.target().text(fui::makeRect(header.x, header.y, header.width - helpWidth, header.height), label, title);
  if (help)
    drawButton(screen, fui::makeRect(header.right() - helpWidth, header.y, helpWidth, header.height),
               tr(STR_BREATH_HOW), HOW, false, false);
}

void BreathworkActivity::buildScreen(UiScreen& screen) {
  focusCount = 0;
  uiTarget.setFont(fui::GfxRendererTarget::FONT_TITLE, uiBreathworkDisplayFontId());
  const auto& theme = screen.theme();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y), static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  screen.insetContent(fui::Insets{theme.spaceLg, static_cast<int16_t>(theme.spaceLg * 2), theme.spaceLg,
                                  static_cast<int16_t>(theme.spaceLg * 2)});
  switch (view) {
    case View::Presets:
      buildPresets(screen);
      break;
    case View::Setup:
      buildSetup(screen);
      break;
    case View::Help:
      buildHelp(screen);
      break;
    case View::Ready:
    case View::Session:
      buildSession(screen);
      break;
    case View::Complete:
      buildComplete(screen);
      break;
  }
  if (view == View::Setup) {
    static constexpr int16_t ORDER[] = {HOW, CYCLES_BASE, CYCLES_BASE + 1, CYCLES_BASE + 2, FASTER, STEADY, START};
    std::copy(std::begin(ORDER), std::end(ORDER), focusTargets);
    focusCount = sizeof(ORDER) / sizeof(ORDER[0]);
    if (preset == BOX_PRESET) {
      focusTargets[4] = BOX_MINUS;
      focusTargets[5] = BOX_PLUS;
    }
  } else if (view == View::Complete) {
    focusTargets[0] = AGAIN;
    focusTargets[1] = DONE;
  }
  bool found = false;
  for (uint8_t i = 0; i < focusCount; ++i) found |= focusTargets[i] == selectedControl;
  if (!found && focusCount) selectedControl = focusTargets[0];
  uiTarget.setFont(fui::GfxRendererTarget::FONT_TITLE, uiScaleSpec().titleFontId);
}

void BreathworkActivity::buildPresets(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const int16_t small = screen.target().lineHeight(theme.smallText.font);
  drawHeader(screen, tr(STR_BREATHWORK));
  drawText(screen, screen.takeTop(line, theme.spaceMd), tr(STR_BREATH_INTRO));
  const int16_t gap = theme.spaceMd;
  const int16_t row = std::min<int16_t>(theme.rowHeight + line, (screen.contentRect().height - gap * 3) / 4);
  const int16_t artHeight =
      std::min<int16_t>(theme.rowHeight * 2, std::max<int16_t>(0, screen.contentRect().height - row * 4 - gap * 4));
  if (artHeight >= theme.minTouchSize)
    ui_breathwork::guide(screen.target(), screen.takeTop(artHeight, gap), theme, 650);
  for (int i = 0; i < 4; ++i) {
    const auto rect = screen.takeTop(row, gap);
    drawButton(screen, rect, nullptr, PRESET_BASE + i);
    const int16_t side = std::min<int16_t>(theme.minTouchSize, row - theme.spaceMd * 2);
    ui_breathwork::presetIcon(screen.target(),
                              fui::makeRect(rect.x + theme.spaceMd, rect.y + (row - side) / 2, side, side), theme, i);
    const int16_t x = rect.x + side + theme.spaceMd * 3;
    const int16_t width = rect.right() - x - theme.spaceMd;
    auto name = theme.bodyText;
    name.bold = true;
    screen.target().text(fui::makeRect(x, rect.y + (row - line - small) / 2, width, line), I18N.get(PRESETS[i].name),
                         name);
    screen.target().text(fui::makeRect(x, rect.y + (row + line - small) / 2, width, small), I18N.get(PRESETS[i].rhythm),
                         theme.smallText);
  }
}

void BreathworkActivity::drawPattern(UiScreen& screen, fui::Rect rect, bool active) {
  const auto& theme = screen.theme();
  const int16_t small = screen.target().lineHeight(theme.smallText.font);
  int count = 0;
  for (uint8_t i = 0; i < 4; ++i)
    if (phaseCount(i)) ++count;
  const int16_t width = (rect.width - theme.spaceSm * (count - 1)) / count;
  int cell = 0;
  for (int i = 0; i < 4; ++i) {
    if (!phaseCount(i)) continue;
    const auto area = fui::makeRect(rect.x + cell++ * (width + theme.spaceSm), rect.y, width, rect.height);
    auto text = theme.smallText;
    text.align = fui::TextAlign::Center;
    const bool current = active && !session.isPaused() && static_cast<int>(session.phase()) == i;
    if (current) {
      screen.target().fill(area, fui::Paint::solid(text.color));
      text.color = fui::invertedColor(text.color);
    } else
      screen.target().stroke(area, fui::Paint::solid(text.color), 1);
    screen.target().text(fui::makeRect(area.x, area.y + theme.spaceXs, area.width, small), I18N.get(PHASE_NAMES[i]),
                         text);
    char value[8];
    snprintf(value, sizeof(value), tr(STR_BREATH_NUMBER), static_cast<unsigned>(phaseCount(i)));
    text.bold = true;
    screen.target().text(fui::makeRect(area.x, area.y + small, area.width, rect.height - small - theme.spaceXs), value,
                         text);
  }
}

void BreathworkActivity::buildSetup(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t small = screen.target().lineHeight(theme.smallText.font);
  drawHeader(screen, I18N.get(PRESETS[preset].name), true);
  auto description = theme.bodyText;
  description.maxLines = 2;
  const char* descriptionText = I18N.get(PRESETS[preset].description);
  const int16_t descriptionHeight =
      fui::measureWrappedText(screen.target(), descriptionText, description, screen.contentRect().width).height;
  screen.target().text(screen.takeTop(descriptionHeight, theme.spaceMd), descriptionText, description);
  const bool compact = screen.contentRect().height < theme.rowHeight * 8;
  const int16_t row = compact ? theme.minTouchSize : theme.rowHeight;
  drawButton(screen, screen.takeBottom(row, theme.spaceMd), tr(STR_BREATH_BEGIN), START, true);
  auto note = theme.smallText;
  note.align = fui::TextAlign::Center;
  note.maxLines = 2;
  screen.target().text(screen.takeBottom(small * 2, theme.spaceSm), tr(STR_BREATH_COMFORT_SHORT), note);
  const auto pace = screen.takeBottom(row, theme.spaceMd);
  screen.target().text(screen.takeBottom(small, theme.spaceSm),
                       preset == BOX_PRESET ? tr(STR_BREATH_PHASE_SECONDS) : tr(STR_BREATH_PACE), theme.smallText);
  const auto rounds = screen.takeBottom(row, theme.spaceMd);
  char summary[64];
  char duration[16];
  timeLabel(duration, sizeof(duration), durationMs());
  snprintf(summary, sizeof(summary), tr(STR_BREATH_LENGTH), cycles(), duration);
  screen.target().text(screen.takeBottom(small, theme.spaceSm), summary, theme.smallText);
  if (preset == BOX_PRESET) {
    const int16_t stepWidth = pace.height;
    drawButton(screen, fui::makeRect(pace.x, pace.y, stepWidth, pace.height), tr(STR_BREATH_MINUS), BOX_MINUS);
    drawButton(screen, fui::makeRect(pace.right() - stepWidth, pace.y, stepWidth, pace.height), tr(STR_BREATH_PLUS),
               BOX_PLUS);
    char seconds[16];
    snprintf(seconds, sizeof(seconds), tr(STR_BREATH_SECONDS_VALUE), static_cast<unsigned>(boxSeconds));
    drawText(screen,
             fui::makeRect(pace.x + stepWidth + theme.spaceSm, pace.y, pace.width - 2 * (stepWidth + theme.spaceSm),
                           pace.height),
             seconds, true, fui::TextAlign::Center);
  } else {
    const int16_t half = (pace.width - theme.spaceSm) / 2;
    drawButton(screen, fui::makeRect(pace.x, pace.y, half, pace.height), tr(STR_BREATH_FASTER), FASTER, countMs == 750);
    drawButton(screen, fui::makeRect(pace.right() - half, pace.y, half, pace.height), tr(STR_BREATH_STEADY), STEADY,
               countMs == 1000);
  }
  const int16_t third = (rounds.width - theme.spaceSm * 2) / 3;
  for (int i = 0; i < 3; ++i) {
    char label[32];
    snprintf(label, sizeof(label), tr(STR_BREATH_NUMBER), static_cast<unsigned>(PRESETS[preset].cycles[i]));
    drawButton(screen, fui::makeRect(rounds.x + i * (third + theme.spaceSm), rounds.y, third, rounds.height), label,
               CYCLES_BASE + i, cycleChoice == i);
  }
  const auto pattern = screen.takeBottom(small * 2 + theme.spaceMd, theme.spaceLg);
  drawPattern(screen, pattern);
  ui_breathwork::guide(screen.target(), screen.contentRect(), theme, 700);
}

void BreathworkActivity::buildHelp(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  drawHeader(screen, I18N.get(PRESETS[preset].name));
  const auto buttons = screen.takeBottom(theme.rowHeight, theme.spaceMd);
  const int16_t third = (buttons.width - theme.spaceSm * 2) / 3;
  drawButton(screen, fui::makeRect(buttons.x, buttons.y, third, buttons.height), tr(STR_BREATH_PREVIOUS),
             HELP_PREVIOUS);
  drawButton(screen, fui::makeRect(buttons.x + third + theme.spaceSm, buttons.y, third, buttons.height),
             tr(STR_BREATH_NEXT), HELP_NEXT);
  drawButton(screen, fui::makeRect(buttons.right() - third, buttons.y, third, buttons.height), tr(STR_DONE), CLOSE_HELP,
             true);
  const StrId HEADINGS[] = {StrId::STR_BREATH_TECHNIQUE,
                            preset == BOX_PRESET ? StrId::STR_BREATH_PHASE_SECONDS : StrId::STR_BREATH_PACE,
                            StrId::STR_BREATH_COMFORT_TITLE, StrId::STR_BREATH_PAUSE_TITLE};
  char heading[96];
  snprintf(heading, sizeof(heading), tr(STR_BREATH_GUIDE_PAGE), helpPage + 1, I18N.get(HEADINGS[helpPage]));
  drawText(screen, screen.takeTop(line, theme.spaceLg), heading);
  auto text = theme.bodyText;
  text.maxLines = 12;
  const StrId paragraphs[] = {PRESETS[preset].instructions,
                              preset == BOX_PRESET ? StrId::STR_BREATH_BOX_SECONDS_HELP : StrId::STR_BREATH_PACE_HELP,
                              StrId::STR_BREATH_COMFORT, StrId::STR_BREATH_RESUME_HELP};
  const char* label = I18N.get(paragraphs[helpPage]);
  const int16_t height = fui::measureWrappedText(screen.target(), label, text, screen.contentRect().width).height;
  screen.target().text(screen.takeTop(height, theme.spaceLg), label, text);
  if (helpPage == 0) drawPattern(screen, screen.takeTop(line * 2 + theme.spaceMd, theme.spaceLg));
}

void BreathworkActivity::buildSession(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
  const int16_t small = screen.target().lineHeight(theme.smallText.font);
  const bool ready = view == View::Ready;
  const bool paused = session.isPaused();
  drawHeader(screen, I18N.get(PRESETS[preset].name), !ready);
  const auto buttons = screen.takeBottom(theme.rowHeight, theme.spaceMd);
  if (ready)
    drawButton(screen, buttons, tr(STR_CANCEL), CANCEL_READY);
  else {
    const int16_t half = (buttons.width - theme.spaceMd) / 2;
    drawButton(screen, fui::makeRect(buttons.x, buttons.y, half, buttons.height),
               paused ? tr(STR_RESUME) : tr(STR_POMODORO_PAUSE), PAUSE, true);
    drawButton(screen, fui::makeRect(buttons.right() - half, buttons.y, half, buttons.height), tr(STR_BREATH_END), END);
  }
  const auto progress = screen.takeBottom(theme.progressHeight * 2, theme.spaceLg);
  const auto status = screen.takeBottom(line, theme.spaceMd);
  const auto hint = screen.takeBottom(line * 2, theme.spaceMd);
  const auto pattern = screen.takeTop(small * 2 + theme.spaceMd, theme.spaceMd);
  if (!ready) drawPattern(screen, pattern, true);
  const auto guide = screen.contentRect();
  const uint16_t phaseProgress = session.phaseProgressPermille(renderAt);
  const auto phase = session.phase();
  const uint16_t expansion = ready || paused                             ? 450
                             : phase == BreathworkSession::Phase::Inhale ? phaseProgress
                             : phase == BreathworkSession::Phase::Hold   ? 1000
                             : phase == BreathworkSession::Phase::Exhale ? 1000 - phaseProgress
                                                                         : 0;
  ui_breathwork::guide(screen.target(), guide, theme, expansion, paused);
  const int16_t centerY = guide.y + guide.height / 2;
  const int16_t centerWidth = std::min<int16_t>(guide.width, std::min(guide.width, guide.height) / 2);
  const int16_t centerX = guide.x + (guide.width - centerWidth) / 2;
  auto cue = theme.titleText;
  cue.align = fui::TextAlign::Center;
  cue.bold = true;
  const char* label = ready    ? tr(STR_BREATH_READY)
                      : paused ? tr(STR_POMODORO_PAUSED)
                               : I18N.get(PHASE_NAMES[static_cast<int>(phase)]);
  if (screen.target().measureText(cue.font, label, cue).width > centerWidth) cue.font = theme.bodyText.font;
  screen.target().text(fui::makeRect(centerX, centerY - titleLine, centerWidth, titleLine), label, cue);
  char number[12];
  snprintf(number, sizeof(number), tr(STR_BREATH_NUMBER),
           ready ? static_cast<unsigned>(3 - std::min<uint32_t>(2, (renderAt - readyAt) / 1000))
                 : static_cast<unsigned>(session.remainingCounts(renderAt)));
  if (!paused)
    drawText(screen, fui::makeRect(centerX, centerY, centerWidth, titleLine), number, true, fui::TextAlign::Center);
  drawText(screen, hint,
           ready                                       ? tr(STR_BREATH_READY_HINT)
           : paused                                    ? tr(STR_BREATH_PAUSED_HINT)
           : phase == BreathworkSession::Phase::Inhale ? tr(STR_BREATH_INHALE_HINT)
           : phase == BreathworkSession::Phase::Exhale ? tr(STR_BREATH_EXHALE_HINT)
                                                       : tr(STR_BREATH_HOLD_HINT),
           false, fui::TextAlign::Center);
  if (!ready) {
    char duration[16];
    char summary[64];
    timeLabel(duration, sizeof(duration), session.remainingMs(renderAt));
    snprintf(summary, sizeof(summary), tr(STR_BREATH_PROGRESS), session.completedCycles() + 1, session.totalCycles(),
             duration);
    drawText(screen, status, summary, false, fui::TextAlign::Center);
    const auto ink = fui::Paint::solid(theme.bodyText.color);
    screen.target().stroke(progress, ink, 1);
    const uint32_t elapsed = session.totalMs() - session.remainingMs(renderAt);
    const auto track = progress.inset(fui::makeInsets(2));
    screen.target().fill(
        fui::makeRect(track.x, track.y,
                      static_cast<uint64_t>(track.width) * elapsed / std::max<uint32_t>(1, session.totalMs()),
                      track.height),
        ink);
  }
}

void BreathworkActivity::buildComplete(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const int16_t title = screen.target().lineHeight(theme.titleText.font);
  drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceMd), tr(STR_DONE), DONE, true);
  drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_BREATH_AGAIN), AGAIN);
  drawHeader(screen, tr(STR_BREATHWORK));
  const auto summary = screen.takeBottom(line * 2, theme.spaceLg);
  const auto message = screen.takeBottom(title * 2, theme.spaceMd);
  ui_breathwork::completion(screen.target(), screen.contentRect(), theme);
  drawText(screen, message, endedEarly ? tr(STR_BREATH_ENDED) : tr(STR_BREATH_COMPLETE), true, fui::TextAlign::Center);
  char label[64];
  snprintf(label, sizeof(label), tr(STR_BREATH_COMPLETED_ROUNDS), session.completedCycles());
  drawText(screen, summary, label, false, fui::TextAlign::Center);
}

void BreathworkActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  const uint32_t now = millis();
  const bool cycleStart =
      session.phase() == BreathworkSession::Phase::Inhale && session.remainingCounts(renderAt) == phaseCount(0);
  const bool clean = cleanRefresh || (now - lastCleanRefreshAt >= 180000 && (!session.isRunning() || cycleStart));
  renderer.displayBuffer(clean ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  if (clean) lastCleanRefreshAt = now;
  cleanRefresh = false;
}
