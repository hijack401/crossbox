#include "CasinoActivity.h"

#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiCasino.h"
#include "components/UiCasinoTable.h"
#include "components/UiFarkle.h"
#include "util/CasinoDate.h"

namespace fui = freeink::ui;
namespace {
using Phase = BlackjackGame::Phase;
using Result = BlackjackGame::Result;
constexpr StrId RULE_TEXT[] = {StrId::STR_CASINO_RULE_GOAL,   StrId::STR_CASINO_RULE_DECK,
                               StrId::STR_CASINO_RULE_PAYOUT, StrId::STR_CASINO_RULE_ACTIONS,
                               StrId::STR_CASINO_RULE_SPLIT,  StrId::STR_CASINO_RULE_ACES,
                               StrId::STR_CASINO_RULE_INSURE, StrId::STR_CASINO_RULE_SURRENDER,
                               StrId::STR_CASINO_RULE_SAVE,   StrId::STR_CASINO_RULE_CREDIT};
constexpr StrId BACCARAT_RULE_TEXT[] = {StrId::STR_BACCARAT_RULE_GOAL,    StrId::STR_BACCARAT_RULE_VALUES,
                                        StrId::STR_BACCARAT_RULE_NATURAL, StrId::STR_BACCARAT_RULE_PLAYER,
                                        StrId::STR_BACCARAT_RULE_BANKER,  StrId::STR_BACCARAT_RULE_BANKER_THIRD,
                                        StrId::STR_BACCARAT_RULE_PAYOUT,  StrId::STR_BACCARAT_RULE_TIE,
                                        StrId::STR_BACCARAT_RULE_SAVE,    StrId::STR_CASINO_RULE_CREDIT};

constexpr StrId ROULETTE_RULE_TEXT[] = {StrId::STR_ROULETTE_RULE_WHEEL,  StrId::STR_ROULETTE_RULE_BETS,
                                        StrId::STR_ROULETTE_RULE_INSIDE, StrId::STR_ROULETTE_RULE_OUTSIDE,
                                        StrId::STR_ROULETTE_RULE_ZERO,   StrId::STR_ROULETTE_RULE_SAVE,
                                        StrId::STR_CASINO_RULE_CREDIT};

constexpr StrId SLOTS_RULE_TEXT[] = {StrId::STR_SLOTS_RULE_PLAY, StrId::STR_SLOTS_RULE_PAYOUT,
                                     StrId::STR_SLOTS_RULE_REELS, StrId::STR_SLOTS_RULE_SAVE,
                                     StrId::STR_CASINO_RULE_CREDIT};

constexpr StrId FARKLE_RULE_TEXT[] = {StrId::STR_FARKLE_RULE_GOAL,   StrId::STR_FARKLE_RULE_SELECT,
                                      StrId::STR_FARKLE_RULE_BUST,   StrId::STR_FARKLE_RULE_HOT,
                                      StrId::STR_FARKLE_RULE_COMBOS, StrId::STR_FARKLE_RULE_MATCH};

const char* baccaratBetLabel(BaccaratGame::Bet bet) {
  switch (bet) {
    case BaccaratGame::Bet::Player:
      return tr(STR_BACCARAT_PLAYER);
    case BaccaratGame::Bet::Banker:
      return tr(STR_BACCARAT_BANKER);
    case BaccaratGame::Bet::Tie:
      return tr(STR_BACCARAT_TIE);
  }
  return tr(STR_BACCARAT_TIE);
}

const char* resultLabel(Result result) {
  switch (result) {
    case Result::Win:
      return tr(STR_CASINO_WIN);
    case Result::Lose:
      return tr(STR_CASINO_LOSE);
    case Result::Push:
      return tr(STR_CASINO_PUSH);
    case Result::Blackjack:
      return tr(STR_CASINO_NATURAL);
    case Result::Bust:
      return tr(STR_CASINO_BUST);
    case Result::Surrender:
      return tr(STR_CASINO_SURRENDERED);
    default:
      return tr(STR_CASINO_YOU);
  }
}

void netMoney(char* out, size_t size, int64_t net) {
  char amount[48];
  ui_casino::money(amount, sizeof(amount), net < 0 ? -net : net);
  if (net == 0)
    snprintf(out, size, "%s", amount);
  else
    snprintf(out, size, net > 0 ? tr(STR_CASINO_PLUS) : tr(STR_CASINO_MINUS), amount);
}
}  // namespace

CasinoActivity::CasinoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Casino", renderer, mappedInput), UiAppHost(renderer) {}

uint32_t CasinoActivity::randomWord(void*) {
  return (static_cast<uint32_t>(random(65536)) << 16) | static_cast<uint32_t>(random(65536));
}

void CasinoActivity::onEnter() {
  Activity::onEnter();
#ifdef SIMULATOR
  // The desktop Arduino shim has no randomSeed and otherwise restarts the same sequence.
  static bool seeded = false;
  if (!seeded) {
    std::srand(static_cast<unsigned>(std::time(nullptr)) ^ static_cast<unsigned>(micros()));
    seeded = true;
  }
#endif
  resetUi();
  app.on(ACTION_CONTROL, &CasinoActivity::onControl, this);
  app.setScreen(&CasinoActivity::casinoScreen, this);
  loadFailed = !store.load();
  if (loadFailed)
    view = View::StorageError;
  else {
    checkDailyCredit();
    if (!dirty && store.loadStatus() == CasinoStore::LoadStatus::Empty) saveChanges();
    normalizeBet();
  }
  requestUpdate();
}

void CasinoActivity::onExit() {
  closeRouting();
  app.clearTapFlash();
  if (dirty && !store.save()) LOG_ERR("CASINO", "Casino transaction remains unsaved on exit");
  Activity::onExit();
}

void CasinoActivity::normalizeBet() {
  const int64_t reserved = tableGame == Game::Roulette ? store.roulette().state().wagerCents : 0;
  const int64_t available = std::max<int64_t>(0, store.game().state().balanceCents - reserved) / 100 * 100;
  betCents = available >= BlackjackGame::MIN_BET_CENTS ? std::clamp<int64_t>(betCents, 100, available) : 100;
}

bool CasinoActivity::canPlaceBet(int64_t cents) const {
  if (tableGame == Game::Farkle) return store.farkle().canBet(cents, store.game().state().balanceCents);
  if (tableGame == Game::Slots) return store.slots().canBet(cents, store.game().state().balanceCents);
  if (tableGame == Game::Roulette)
    return store.roulette().canAddBet(rouletteBet, cents, store.game().state().balanceCents);
  return tableGame == Game::Baccarat ? store.baccarat().canBet(cents, store.game().state().balanceCents)
                                     : store.game().canBet(cents);
}

bool CasinoActivity::saveChanges() {
  dirty = true;
  if (store.save()) {
    dirty = false;
    return true;
  }
  if (view != View::StorageError) storageReturnView = view;
  view = View::StorageError;
  selectedControl = RETRY;
  return false;
}

void CasinoActivity::checkDailyCredit() {
  lastClockCheck = millis();
  if (loadFailed || dirty) return;
  struct tm local{};
  const int32_t day = halClock.localTime(local) ? casino_date::civilDay(local) : 0;
  const bool wasValid = clockValid;
  clockValid = day != 0;
  if (store.game().applyDailyCredit(day)) {
    saveChanges();
    normalizeBet();
    refresh();
  } else if (wasValid != clockValid)
    refresh();
}

void CasinoActivity::refresh() {
  closeRouting();
  app.clearTapFlash();
  requestUpdate();
}

bool CasinoActivity::handleHomeGesture() {
  RenderLock lock;
  if (dirty) {
    view = View::StorageError;
    selectedControl = RETRY;
    refresh();
    requestUpdate(true);
  } else {
    lock.unlock();
    onGoHome(HomeMenuItem::APPS);
  }
  return true;
}

void CasinoActivity::goBack() {
  RenderLock lock;
  if (dirty) {
    view = View::StorageError;
    selectedControl = RETRY;
  } else if (view == View::Rules) {
    view = returnView;
    if (view == View::Table && tableGame == Game::Farkle) selectedControl = farklePrimaryControl();
  } else if (view == View::BetEntry) {
    view = tableGame == Game::Roulette ? View::RouletteStake : View::Table;
  } else if (view == View::RouletteStake || view == View::RouletteOptions) {
    view = View::RoulettePicker;
    roulettePage = 0;
  } else if (view == View::RoulettePicker) {
    view = View::Table;
    selectedControl = R_ADD;
  } else if (view == View::Table) {
    view = View::Lobby;
    refreshPolicy.allowCleaning();
    selectedControl = tableGame == Game::Farkle     ? FARKLE
                      : tableGame == Game::Slots    ? SLOTS
                      : tableGame == Game::Roulette ? ROULETTE
                      : tableGame == Game::Baccarat ? BACCARAT
                                                    : BLACKJACK;
  } else {
    lock.unlock();
    activityManager.goToApps(AppMenuItem::CASINO);
    return;
  }
  refresh();
}

int64_t CasinoActivity::enteredBet() const {
  int64_t value = 0;
  for (const char* c = betInput; *c; ++c) value = value * 10 + (*c - '0');
  return value * 100;
}

void CasinoActivity::activate(const int control) {
  if (control == CLOSE) {
    goBack();
    return;
  }
  RenderLock lock;
  auto& game = store.game();
  if (view == View::StorageError) {
    if (control != RETRY) return;
    if (loadFailed) {
      loadFailed = !store.load();
      if (!loadFailed) {
        view = View::Lobby;
        checkDailyCredit();
        if (!dirty && store.loadStatus() == CasinoStore::LoadStatus::Empty) saveChanges();
        normalizeBet();
      }
    } else if (store.save()) {
      dirty = false;
      view = storageReturnView;
    }
    refresh();
    return;
  }
  if (control == RULES && (view == View::Lobby || view == View::Table)) {
    returnView = view;
    rulesGame = view == View::Table ? tableGame : Game::Blackjack;
    view = View::Rules;
    rulesPage = 0;
  } else if (view == View::Rules) {
    if (control == NEXT_RULE) ++rulesPage;
    if (control == PREVIOUS_RULE) rulesPage = std::max(0, rulesPage - 1);
    if (control == RULES_BLACKJACK || control == RULES_BACCARAT || control == RULES_ROULETTE ||
        control == RULES_SLOTS || control == RULES_FARKLE) {
      rulesGame = control == RULES_FARKLE     ? Game::Farkle
                  : control == RULES_SLOTS    ? Game::Slots
                  : control == RULES_ROULETTE ? Game::Roulette
                  : control == RULES_BACCARAT ? Game::Baccarat
                                              : Game::Blackjack;
      rulesPage = 0;
    }
  } else if (view == View::Lobby && (control == BLACKJACK || control == BACCARAT || control == ROULETTE ||
                                     control == SLOTS || control == FARKLE)) {
    tableGame = control == FARKLE     ? Game::Farkle
                : control == SLOTS    ? Game::Slots
                : control == ROULETTE ? Game::Roulette
                : control == BACCARAT ? Game::Baccarat
                                      : Game::Blackjack;
    view = View::Table;
    displayedHand = game.state().activeHand;
    normalizeBet();
    if (tableGame == Game::Farkle) {
      farkleSelection = 0;
      selectedControl = farklePrimaryControl();
    } else if (tableGame == Game::Slots) {
      const auto& state = store.slots().state();
      selectedControl = state.phase == SlotsGame::Phase::Betting     ? S_SPIN
                        : state.phase == SlotsGame::Phase::Revealing ? S_REVEAL
                                                                     : S_AGAIN;
    } else if (tableGame == Game::Roulette) {
      const auto phase = store.roulette().state().phase;
      selectedControl = phase == RouletteGame::Phase::Betting    ? R_ADD
                        : phase == RouletteGame::Phase::Spinning ? R_REVEAL
                                                                 : R_NEW;
    } else if (tableGame == Game::Baccarat) {
      const auto& state = store.baccarat().state();
      if (state.phase != BaccaratGame::Phase::Betting) baccaratBet = state.bet;
      selectedControl = state.phase == BaccaratGame::Phase::Betting   ? DEAL
                        : state.phase == BaccaratGame::Phase::Settled ? AGAIN
                                                                      : REVEAL_CARD;
    } else
      selectedControl = game.state().phase == Phase::Betting     ? DEAL
                        : game.state().phase == Phase::Settled   ? AGAIN
                        : game.state().phase == Phase::Insurance ? DECLINE
                                                                 : HIT;
  } else if (view == View::BetEntry) {
    const size_t length = strlen(betInput);
    if (control >= DIGIT_BASE && control < DIGIT_BASE + 10 && length < sizeof(betInput) - 1) {
      if (!(length == 0 && control == DIGIT_BASE)) {
        betInput[length] = static_cast<char>('0' + control - DIGIT_BASE);
        betInput[length + 1] = '\0';
      }
    } else if (control == ERASE && length)
      betInput[length - 1] = '\0';
    else if (control == ACCEPT_BET && canPlaceBet(enteredBet())) {
      betCents = enteredBet();
      view = tableGame == Game::Roulette ? View::RouletteStake : View::Table;
      selectedControl = tableGame == Game::Farkle     ? F_START
                        : tableGame == Game::Slots    ? S_SPIN
                        : tableGame == Game::Roulette ? R_ACCEPT
                                                      : DEAL;
    }
  } else if (tableGame == Game::Farkle && view == View::Table) {
    activateFarkle(control);
  } else if (tableGame == Game::Slots && view == View::Table) {
    activateSlots(control);
  } else if (tableGame == Game::Roulette && (view == View::Table || view == View::RoulettePicker ||
                                             view == View::RouletteOptions || view == View::RouletteStake)) {
    activateRoulette(control);
  } else if (view == View::Table && tableGame == Game::Baccarat) {
    activateBaccarat(control);
  } else if (view == View::Table) {
    if (game.state().phase == Phase::Betting) {
      if (control >= PRESET_BASE && control < PRESET_BASE + 4 && game.canBet(PRESETS[control - PRESET_BASE]))
        betCents = PRESETS[control - PRESET_BASE];
      else if (control == CUSTOM) {
        betInput[0] = '\0';
        view = View::BetEntry;
      } else if (control == MAX_BET) {
        betCents = game.state().balanceCents / 100 * 100;
        normalizeBet();
      } else if (control == DEAL && game.startRound(betCents, &CasinoActivity::randomWord)) {
        displayedHand = 0;
        selectedControl = game.state().phase == Phase::Settled     ? AGAIN
                          : game.state().phase == Phase::Insurance ? DECLINE
                                                                   : HIT;
        saveChanges();
      }
    } else if (control >= HAND_BASE && control < HAND_BASE + game.state().handCount &&
               game.state().phase == Phase::Settled) {
      displayedHand = control - HAND_BASE;
    } else {
      bool changed = false;
      switch (control) {
        case HIT:
          changed = game.hit();
          break;
        case STAND:
          changed = game.stand();
          break;
        case DOUBLE:
          changed = game.doubleDown();
          break;
        case SPLIT:
          changed = game.split();
          break;
        case SURRENDER:
          changed = game.surrender();
          break;
        case INSURE:
          changed = game.insurance(true);
          break;
        case DECLINE:
          changed = game.insurance(false);
          break;
        case AGAIN:
          changed = game.nextRound();
          if (changed) refreshPolicy.allowCleaning();
          normalizeBet();
          break;
        default:
          break;
      }
      if (changed) {
        displayedHand = std::min<int>(game.state().activeHand, std::max(0, game.state().handCount - 1));
        selectedControl = game.state().phase == Phase::Betting   ? DEAL
                          : game.state().phase == Phase::Settled ? AGAIN
                                                                 : HIT;
        saveChanges();
      }
    }
  }
  refresh();
}

void CasinoActivity::activateBaccarat(int control) {
  auto& game = store.baccarat();
  if (game.state().phase == BaccaratGame::Phase::Revealing) {
    if (control == REVEAL_CARD && store.revealBaccarat()) {
      selectedControl = game.state().phase == BaccaratGame::Phase::Settled ? AGAIN : REVEAL_CARD;
      saveChanges();
    }
    return;
  }
  if (game.state().phase == BaccaratGame::Phase::Settled) {
    if (control == AGAIN && game.nextRound()) {
      refreshPolicy.allowCleaning();
      normalizeBet();
      selectedControl = DEAL;
      saveChanges();
    }
    return;
  }
  if (control >= BET_PLAYER && control <= BET_TIE) {
    baccaratBet = static_cast<BaccaratGame::Bet>(control - BET_PLAYER);
  } else if (control >= PRESET_BASE && control < PRESET_BASE + 4 && canPlaceBet(PRESETS[control - PRESET_BASE])) {
    betCents = PRESETS[control - PRESET_BASE];
  } else if (control == CUSTOM) {
    betInput[0] = '\0';
    view = View::BetEntry;
  } else if (control == MAX_BET) {
    betCents = store.game().state().balanceCents / 100 * 100;
    normalizeBet();
  } else if (control == DEAL && store.dealBaccarat(baccaratBet, betCents, &CasinoActivity::randomWord)) {
    selectedControl = REVEAL_CARD;
    saveChanges();
  }
}

void CasinoActivity::loop() {
  {
    RenderLock lock;
    if (millis() - lastClockCheck >= 30000) checkDailyCredit();
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

void CasinoActivity::onControl(const fui::ActionEvent& event, void* user) {
  static_cast<CasinoActivity*>(user)->activate(event.value);
}

void CasinoActivity::casinoScreen(UiScreen& screen, void* user) {
  static_cast<CasinoActivity*>(user)->buildScreen(screen);
}

void CasinoActivity::drawLabel(UiScreen& screen, fui::Rect rect, const char* label, bool title, fui::TextAlign align) {
  auto style = title ? screen.theme().titleText : screen.theme().bodyText;
  style.bold = title;
  style.align = align;
  style.maxLines = std::max<int>(1, rect.height / screen.target().lineHeight(style.font));
  screen.target().text(rect, label, style);
}

void CasinoActivity::drawButton(UiScreen& screen, fui::Rect rect, const char* label, int control, bool primary,
                                bool enabled, bool outlined) {
  auto& props = buttonProps;
  props.label = control == RULES ? nullptr : label;
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
  const bool tableControl = view == View::Table || (view == View::Rules && rulesGame == Game::Farkle);
  if (tableControl && outlined) {
    props.styles.disabled.border = ink;
    props.styles.disabled.borderWidth = 1;
  }
  props.styles.focused = primary && tableControl ? props.styles.selected : props.styles.normal;
  props.styles.focused.border = ink;
  props.styles.focused.borderWidth = control == RULES ? 0 : 3;
  if (buttonNavigation && selectedControl == control) props.state = fui::StateFocused;
  fui::button(screen.frame(), rect, props);
  if (tableControl && primary && enabled && buttonNavigation && selectedControl == control)
    screen.target().stroke(rect.inset(fui::makeInsets(screen.theme().spaceSm)),
                           fui::Paint::solid(fui::invertedColor(screen.theme().bodyText.color)), 1);
  if (control == RULES) {
    const auto state = screen.frame().stateFor(props.action, props.value, props.state);
    auto text = fui::textStyleWithForeground(props.text, props.styles.resolve(state).foreground);
    text.align = fui::TextAlign::Right;
    screen.target().text(rect, label, text);
    if (buttonNavigation && selectedControl == RULES) {
      const int16_t width = std::min<int16_t>(rect.width, screen.target().measureText(text.font, label, text).width);
      const int16_t y = rect.bottom() - screen.theme().spaceSm;
      screen.target().line(fui::Point{static_cast<int16_t>(rect.right() - width), y}, fui::Point{rect.right(), y}, 2,
                           props.styles.resolve(state).foreground);
    }
  }
  if (!enabled) {
    if (primary) {
      // Show the active split hand without registering a tappable control.
      screen.target().fill(rect, ink);
      auto text = screen.theme().bodyText;
      text.bold = true;
      text.align = fui::TextAlign::Center;
      text.color = fui::invertedColor(text.color);
      screen.target().text(rect, label, text);
    } else {
      ui_casino::soften(screen.target(), rect, screen.theme());
    }
  }
  if (enabled && focusCount < 24) focusTargets[focusCount++] = static_cast<int16_t>(control);
}

void CasinoActivity::buildScreen(UiScreen& screen) {
  focusCount = 0;
  const bool gameDisplay = view == View::Table || (view == View::Rules && rulesGame == Game::Farkle);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_TITLE, gameDisplay ? uiGameDisplayFontId() : uiScaleSpec().titleFontId);
  const auto& theme = screen.theme();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y), static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  screen.insetContent(fui::Insets{theme.spaceLg, static_cast<int16_t>(theme.spaceLg * 2), theme.spaceLg,
                                  static_cast<int16_t>(theme.spaceLg * 2)});
  switch (view) {
    case View::Lobby:
      buildLobby(screen);
      break;
    case View::Table:
      buildTable(screen);
      break;
    case View::BetEntry:
      buildBetEntry(screen);
      break;
    case View::Rules:
      buildRules(screen);
      break;
    case View::StorageError:
      buildStorageError(screen);
      break;
    case View::RoulettePicker:
    case View::RouletteOptions:
      buildRoulettePicker(screen);
      break;
    case View::RouletteStake:
      buildRouletteStake(screen);
      break;
  }
  bool focused = false;
  for (int i = 0; i < focusCount; ++i) focused |= focusTargets[i] == selectedControl;
  if (!focused && focusCount) selectedControl = focusTargets[0];
  uiTarget.setFont(fui::GfxRendererTarget::FONT_TITLE, uiScaleSpec().titleFontId);
}

void CasinoActivity::buildLobby(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const auto header = screen.takeTop(
      screen.contentRect().height < theme.rowHeight * 7 ? theme.minTouchSize : theme.rowHeight, theme.spaceMd);
  drawLabel(screen, fui::Rect{header.x, header.y, static_cast<int16_t>(header.width * 2 / 3), header.height},
            tr(STR_CASINO_BALANCE));
  drawButton(screen,
             fui::Rect{static_cast<int16_t>(header.right() - header.width / 3), header.y,
                       static_cast<int16_t>(header.width / 3), header.height},
             tr(STR_CASINO_RULES), RULES, false, true, false);
  const int16_t captionHeight = line * (clockValid ? 1 : 2);
  const int16_t fullMenuHeight = (theme.rowHeight + theme.spaceLg) * 5 + theme.spaceMd * 4;
  const bool compact = screen.contentRect().height < fullMenuHeight + line * 2 + captionHeight + theme.spaceLg;
  const int16_t rowHeight = compact ? theme.minTouchSize : theme.rowHeight + theme.spaceLg;
  const int16_t gap = compact ? theme.spaceSm : theme.spaceMd;
  const auto menu = screen.takeBottom(rowHeight * 5 + gap * 4);
  const auto moneyArea = screen.takeTop(
      std::min<int16_t>(theme.rowHeight,
                        std::max<int16_t>(line, screen.contentRect().height - captionHeight - theme.spaceSm)),
      theme.spaceSm);
  ui_casino::balance(screen.target(), moneyArea, theme, store.game().state().balanceCents);
  auto caption = theme.smallText;
  caption.align = fui::TextAlign::Left;
  caption.bold = false;
  caption.maxLines = clockValid ? 1 : 2;
  screen.target().text(screen.takeTop(captionHeight), clockValid ? tr(STR_CASINO_DAILY) : tr(STR_CASINO_CLOCK),
                       caption);
  int16_t y = menu.y - screen.contentRect().height / 2;
  for (uint8_t i = 0; i < 5; ++i) {
    drawLobbyGame(screen, fui::Rect{menu.x, y, menu.width, rowHeight}, i);
    y += rowHeight + gap;
  }
}

void CasinoActivity::drawLobbyGame(UiScreen& screen, fui::Rect row, uint8_t game) {
  const auto& theme = screen.theme();
  static constexpr StrId GAMES[] = {StrId::STR_CASINO_SLOTS, StrId::STR_CASINO_BLACKJACK, StrId::STR_CASINO_ROULETTE,
                                    StrId::STR_CASINO_BACCARAT, StrId::STR_CASINO_FARKLE};
  if (game > 4) return;
  const int control = game == 0 ? SLOTS : game == 1 ? BLACKJACK : game == 2 ? ROULETTE : game == 3 ? BACCARAT : FARKLE;
  drawButton(screen, row, nullptr, control);
  const auto state = screen.frame().stateFor(ACTION_CONTROL, control, buttonProps.state);
  const auto foreground = buttonProps.styles.resolve(state).foreground;
  auto text = fui::textStyleWithForeground(buttonProps.text, foreground);
  text.align = fui::TextAlign::Left;
  const int16_t side = std::min<int16_t>(theme.minTouchSize, row.height - theme.spaceMd * 2);
  const fui::Rect icon{static_cast<int16_t>(row.x + theme.spaceLg),
                       static_cast<int16_t>(row.y + (row.height - side) / 2), side, side};
  if (game == 4)
    ui_farkle::die(screen.target(), icon, foreground, 5);
  else
    ui_casino::lobbyIcon(screen.target(), icon, theme, game);
  screen.target().text(fui::Rect{static_cast<int16_t>(row.x + side + theme.spaceLg * 2), row.y,
                                 static_cast<int16_t>(row.width - side - theme.spaceLg * 4), row.height},
                       I18N.get(GAMES[game]), text);
  const int16_t x = row.right() - theme.spaceLg * 2;
  const int16_t y = row.y + row.height / 2;
  screen.target().line(fui::Point{x, static_cast<int16_t>(y - theme.spaceSm)},
                       fui::Point{static_cast<int16_t>(x + theme.spaceSm), y}, 2, foreground);
  screen.target().line(fui::Point{static_cast<int16_t>(x + theme.spaceSm), y},
                       fui::Point{x, static_cast<int16_t>(y + theme.spaceSm)}, 2, foreground);
}

void CasinoActivity::buildTable(UiScreen& screen) {
  if (tableGame == Game::Farkle) {
    buildFarkleTable(screen);
    return;
  }
  const auto& theme = screen.theme();
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
  const auto header = screen.takeTop(titleLine + smallLine + theme.spaceSm, theme.spaceSm);
  const char* name = tableGame == Game::Slots      ? tr(STR_CASINO_SLOTS)
                     : tableGame == Game::Roulette ? tr(STR_CASINO_ROULETTE)
                     : tableGame == Game::Baccarat ? tr(STR_CASINO_BACCARAT)
                                                   : tr(STR_CASINO_BLACKJACK);
  const int16_t rulesWidth = header.width / 3;
  drawLabel(screen, fui::makeRect(header.x, header.y, header.width - rulesWidth, titleLine), name, true);
  drawButton(screen, fui::makeRect(header.right() - rulesWidth, header.y, rulesWidth, titleLine),
             tableGame == Game::Slots ? tr(STR_SLOTS_PAYTABLE) : tr(STR_CASINO_RULES), RULES, false, true, false);
  char money[48];
  char balance[80];
  ui_casino::money(money, sizeof(money), store.game().state().balanceCents);
  snprintf(balance, sizeof(balance), tr(STR_CASINO_BALANCE_AMOUNT), money);
  screen.target().text(fui::makeRect(header.x, header.y + titleLine + theme.spaceSm, header.width, smallLine), balance,
                       theme.smallText);
  ui_casino_table::ornament(screen.target(), screen.takeTop(theme.spaceLg, theme.spaceMd), theme);
  if (tableGame == Game::Slots) {
    if (store.slots().state().phase == SlotsGame::Phase::Betting)
      buildSlotsBetting(screen);
    else
      buildSlotsRound(screen);
  } else if (tableGame == Game::Roulette) {
    if (store.roulette().state().phase == RouletteGame::Phase::Betting)
      buildRouletteBetting(screen);
    else
      buildRouletteRound(screen);
  } else if (tableGame == Game::Baccarat && store.baccarat().state().phase != BaccaratGame::Phase::Betting)
    buildBaccaratRound(screen);
  else if (tableGame == Game::Baccarat || store.game().state().phase == Phase::Betting)
    buildBetting(screen);
  else
    buildRound(screen);
}

void CasinoActivity::buildBetting(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const bool compact = screen.contentRect().height < theme.rowHeight * 7;
  const int16_t row = tableGame == Game::Baccarat && compact ? theme.minTouchSize : theme.rowHeight;
  const int16_t gap = tableGame == Game::Baccarat && compact ? theme.spaceSm : theme.spaceMd;
  const auto deal = screen.takeBottom(row, gap);
  const auto custom = screen.takeBottom(row, gap);
  const auto presets = screen.takeBottom(row, gap);
  if (tableGame == Game::Baccarat)
    drawBaccaratBets(screen, screen.takeBottom(std::max<int16_t>(row, line * 2 + theme.spaceSm), gap));
  if (store.game().state().balanceCents < 100) {
    drawLabel(screen, screen.contentRect(), tr(STR_CASINO_NO_FUNDS), false, fui::TextAlign::Center);
  } else {
    auto area = screen.contentRect();
    const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
    ui_casino_table::frame(screen.target(), area, theme);
    area = area.inset(fui::makeInsets(theme.spaceLg));
    const int16_t footerHeight = line + titleLine + theme.spaceSm;
    const int16_t artHeight = std::max<int16_t>(0, area.height - footerHeight - theme.spaceMd);
    if (artHeight >= line * 2) {
      const int16_t cardHeight = std::min<int16_t>(theme.rowHeight * 3, artHeight - theme.spaceSm);
      const int16_t cardWidth = cardHeight * 2 / 3;
      const int16_t cx = area.x + area.width / 2;
      const int16_t top = area.y + (artHeight - cardHeight) / 2;
      const bool baccarat = tableGame == Game::Baccarat;
      ui_casino_table::card(screen.target(), fui::makeRect(cx - cardWidth / 3, top, cardWidth, cardHeight), theme,
                            baccarat ? 2 : 13, baccarat ? 0 : 2);
      ui_casino_table::card(screen.target(),
                            fui::makeRect(cx - cardWidth * 3 / 4, top + theme.spaceSm, cardWidth, cardHeight), theme,
                            baccarat ? 7 : 1, baccarat ? 1 : 3);
      const int16_t chipSide = std::min<int16_t>(theme.minTouchSize, cardWidth / 2);
      if (area.width >= cardWidth * 3)
        ui_casino_table::chip(screen.target(),
                              fui::makeRect(cx + cardWidth * 3 / 4, top + cardHeight - chipSide, chipSide, chipSide),
                              theme, true);
    }
    const auto caption = fui::makeRect(area.x, area.bottom() - footerHeight, area.width, line);
    drawLabel(screen, caption, tr(STR_CASINO_BET), false, fui::TextAlign::Center);
    char amount[48];
    ui_casino::money(amount, sizeof(amount), betCents);
    auto number = theme.titleText;
    number.bold = true;
    number.align = fui::TextAlign::Center;
    if (screen.target().measureText(number.font, amount, number).width > area.width) number.font = theme.bodyText.font;
    screen.target().text(fui::makeRect(area.x, caption.bottom() + theme.spaceSm, area.width, titleLine), amount,
                         number);
  }
  const int16_t cell = (presets.width - theme.spaceSm * 3) / 4;
  for (int i = 0; i < 4; ++i) {
    char amount[24];
    ui_casino::money(amount, sizeof(amount), PRESETS[i]);
    drawButton(screen,
               fui::Rect{static_cast<int16_t>(presets.x + i * (cell + theme.spaceSm)), presets.y, cell, presets.height},
               amount, PRESET_BASE + i, betCents == PRESETS[i], canPlaceBet(PRESETS[i]));
  }
  const int16_t width = (custom.width - theme.spaceMd) / 2;
  drawButton(screen, fui::Rect{custom.x, custom.y, width, custom.height}, tr(STR_CASINO_CUSTOM), CUSTOM, false,
             store.game().state().balanceCents >= 100);
  drawButton(screen, fui::Rect{static_cast<int16_t>(custom.right() - width), custom.y, width, custom.height},
             tr(STR_CASINO_MAX), MAX_BET, false, store.game().state().balanceCents >= 100);
  drawButton(screen, deal, tr(STR_CASINO_DEAL), DEAL, canPlaceBet(betCents), canPlaceBet(betCents));
}

void CasinoActivity::drawBaccaratBets(UiScreen& screen, fui::Rect area) {
  const auto& theme = screen.theme();
  const int16_t width = (area.width - theme.spaceSm * 2) / 3;
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  static constexpr StrId PAYOUTS[] = {StrId::STR_BACCARAT_PLAYER_ODDS, StrId::STR_BACCARAT_BANKER_ODDS,
                                      StrId::STR_BACCARAT_TIE_ODDS};
  for (int i = 0; i < 3; ++i) {
    const auto bet = static_cast<BaccaratGame::Bet>(i);
    const fui::Rect cell{static_cast<int16_t>(area.x + i * (width + theme.spaceSm)), area.y, width, area.height};
    drawButton(screen, cell, nullptr, BET_PLAYER + i, baccaratBet == bet);
    const auto state = screen.frame().stateFor(ACTION_CONTROL, BET_PLAYER + i, buttonProps.state);
    const auto foreground = buttonProps.styles.resolve(state).foreground;
    auto label = fui::textStyleWithForeground(buttonProps.text, foreground);
    label.align = fui::TextAlign::Center;
    auto odds = fui::textStyleWithForeground(theme.smallText, foreground);
    odds.align = fui::TextAlign::Center;
    const int16_t top = cell.y + (cell.height - line - smallLine) / 2;
    screen.target().text(fui::Rect{cell.x, top, cell.width, line}, baccaratBetLabel(bet), label);
    screen.target().text(fui::Rect{cell.x, static_cast<int16_t>(top + line), cell.width, smallLine},
                         I18N.get(PAYOUTS[i]), odds);
  }
}

void CasinoActivity::drawBaccaratHand(UiScreen& screen, fui::Rect area, const BaccaratGame::Hand& hand, bool banker) {
  const auto& theme = screen.theme();
  const auto& state = store.baccarat().state();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const bool complete = state.phase == BaccaratGame::Phase::Settled;
  const bool winner = complete && state.winner == (banker ? BaccaratGame::Bet::Banker : BaccaratGame::Bet::Player);
  ui_casino_table::frame(screen.target(), area, theme);
  area = area.inset(fui::makeInsets(theme.spaceMd));
  const int16_t band = line + theme.spaceSm;
  auto text = theme.bodyText;
  text.bold = true;
  const auto heading = fui::makeRect(area.x, area.y, area.width, band);
  if (winner) {
    screen.target().fill(heading, fui::Paint::solid(text.color));
    text.color = fui::invertedColor(text.color);
  }
  screen.target().text(fui::makeRect(heading.x + theme.spaceSm, heading.y, heading.width * 2 / 3, band),
                       banker ? tr(STR_BACCARAT_BANKER) : tr(STR_BACCARAT_PLAYER), text);
  unsigned visibleTotal = 0;
  uint8_t visible = 0;
  for (uint8_t i = 0; i < hand.cardCount; ++i) {
    if (!store.baccarat().cardRevealed(banker, i)) continue;
    const uint8_t rank = BlackjackGame::rank(hand.cards[i]);
    visibleTotal += rank >= 10 ? 0 : rank;
    ++visible;
  }
  const uint8_t thirdCardTurn = banker ? 2 + state.player.cardCount : 4;
  char total[16];
  if (complete || (visible == hand.cardCount && state.revealedCards >= thirdCardTurn))
    snprintf(total, sizeof(total), tr(STR_CASINO_TOTAL), BaccaratGame::value(hand));
  else if (visible)
    snprintf(total, sizeof(total), tr(STR_CASINO_UNKNOWN_TOTAL), visibleTotal % 10);
  else
    snprintf(total, sizeof(total), "%s", tr(STR_CASINO_CARD_HIDDEN));
  text.align = fui::TextAlign::Right;
  screen.target().text(fui::makeRect(heading.x + heading.width / 2, heading.y, heading.width / 2 - theme.spaceSm, band),
                       total, text);
  area.y += band + theme.spaceSm;
  area.height -= band + theme.spaceSm;
  if (area.empty()) return;
  // Extra cards arrive only once the earlier cards have been turned over.
  const uint8_t cards = !complete && state.revealedCards < thirdCardTurn ? 2 : hand.cardCount;
  const int16_t cardWidth = std::min<int16_t>((area.width - theme.spaceMd * (cards - 1)) / cards,
                                              std::min<int16_t>(theme.rowHeight * 2, area.height * 2 / 3));
  const int16_t cardHeight = cardWidth * 3 / 2;
  const int16_t start = area.x + (area.width - (cardWidth * cards + theme.spaceMd * (cards - 1))) / 2;
  for (uint8_t i = 0; i < cards; ++i)
    ui_casino_table::card(screen.target(),
                          fui::makeRect(start + i * (cardWidth + theme.spaceMd),
                                        area.y + (area.height - cardHeight) / 2, cardWidth, cardHeight),
                          theme, BlackjackGame::rank(hand.cards[i]), BlackjackGame::suit(hand.cards[i]),
                          !store.baccarat().cardRevealed(banker, i));
}

void CasinoActivity::buildBaccaratRound(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& state = store.baccarat().state();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const bool compact = screen.contentRect().height < theme.rowHeight * 7;
  const bool complete = state.phase == BaccaratGame::Phase::Settled;
  drawButton(screen, screen.takeBottom(compact ? theme.minTouchSize : theme.rowHeight, theme.spaceMd),
             complete ? tr(STR_CASINO_AGAIN) : tr(STR_BACCARAT_REVEAL_CARD), complete ? AGAIN : REVEAL_CARD, true);
  const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
  const auto result = screen.takeBottom(complete ? titleLine + line + theme.spaceMd * 2 : line, theme.spaceMd);
  char amount[48];
  char label[80];
  if (complete) {
    ui_casino_table::frame(screen.target(), result, theme);
    drawLabel(
        screen,
        fui::makeRect(result.x + theme.spaceMd, result.y + theme.spaceSm, result.width - theme.spaceMd * 2, titleLine),
        state.winner == BaccaratGame::Bet::Player   ? tr(STR_BACCARAT_PLAYER_WINS)
        : state.winner == BaccaratGame::Bet::Banker ? tr(STR_BACCARAT_BANKER_WINS)
                                                    : tr(STR_BACCARAT_TIE),
        true, fui::TextAlign::Center);
    netMoney(amount, sizeof(amount), state.returnCents - state.wagerCents);
    snprintf(label, sizeof(label), tr(STR_BACCARAT_RETURN),
             state.returnCents > state.wagerCents    ? tr(STR_CASINO_WIN)
             : state.returnCents == state.wagerCents ? tr(STR_CASINO_PUSH)
                                                     : tr(STR_BACCARAT_LOSE),
             amount);
    drawLabel(screen,
              fui::makeRect(result.x + theme.spaceMd, result.y + titleLine + theme.spaceSm,
                            result.width - theme.spaceMd * 2, line),
              label, false, fui::TextAlign::Center);
  } else {
    snprintf(label, sizeof(label), tr(STR_BACCARAT_REVEAL_PROGRESS), static_cast<unsigned>(state.revealedCards));
    auto progress = theme.smallText;
    progress.align = fui::TextAlign::Center;
    screen.target().text(result, label, progress);
  }
  ui_casino::money(amount, sizeof(amount), state.wagerCents);
  snprintf(label, sizeof(label), tr(STR_BACCARAT_WAGER), baccaratBetLabel(state.bet), amount);
  drawLabel(screen, screen.takeTop(line, theme.spaceLg), label, false, fui::TextAlign::Center);
  const auto area = screen.contentRect();
  if (area.width > area.height) {
    const int16_t width = (area.width - theme.spaceLg) / 2;
    drawBaccaratHand(screen, fui::Rect{area.x, area.y, width, area.height}, state.player, false);
    drawBaccaratHand(screen, fui::Rect{static_cast<int16_t>(area.right() - width), area.y, width, area.height},
                     state.banker, true);
  } else {
    const int16_t height = (area.height - theme.spaceLg) / 2;
    drawBaccaratHand(screen, fui::Rect{area.x, area.y, area.width, height}, state.player, false);
    drawBaccaratHand(screen, fui::Rect{area.x, static_cast<int16_t>(area.bottom() - height), area.width, height},
                     state.banker, true);
  }
}

void CasinoActivity::drawHand(UiScreen& screen, fui::Rect area, const BlackjackGame::Hand& hand, bool dealer) {
  const auto& theme = screen.theme();
  const bool hidden = dealer && store.game().state().phase != Phase::Settled;
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  ui_casino_table::frame(screen.target(), area, theme);
  area = area.inset(fui::makeInsets(theme.spaceMd));
  const int16_t band = line + theme.spaceSm;
  auto label = theme.bodyText;
  label.bold = true;
  screen.target().text(fui::makeRect(area.x + theme.spaceSm, area.y, area.width / 2, band),
                       dealer ? tr(STR_CASINO_DEALER) : tr(STR_CASINO_YOU), label);
  char total[32];
  if (hidden)
    snprintf(total, sizeof(total), tr(STR_CASINO_UNKNOWN_TOTAL), BlackjackGame::cardValue(hand.cards[0]));
  else
    snprintf(total, sizeof(total), BlackjackGame::isSoft(hand) ? tr(STR_CASINO_SOFT_TOTAL) : tr(STR_CASINO_TOTAL),
             BlackjackGame::value(hand));
  const int16_t scoreWidth = std::min<int16_t>(
      area.width / 2, screen.target().measureText(label.font, total, label).width + theme.spaceMd * 2);
  const auto score = fui::makeRect(area.right() - scoreWidth, area.y, scoreWidth, band);
  if (!dealer) {
    screen.target().fill(score, fui::Paint::solid(label.color));
    label.color = fui::invertedColor(label.color);
  }
  label.align = fui::TextAlign::Center;
  screen.target().text(score, total, label);
  area.y += band + theme.spaceSm;
  area.height -= band + theme.spaceSm;
  if (area.empty() || hand.cardCount == 0) return;
  auto indexStyle = theme.smallText;
  indexStyle.bold = true;
  const int16_t exposed = screen.target().measureText(indexStyle.font, "10", indexStyle).width + theme.spaceMd * 2;
  const int16_t fullHeight = std::min<int16_t>(theme.rowHeight * 3, area.height);
  const int16_t fullWidth = std::min<int16_t>(area.width, fullHeight * 2 / 3);
  const int16_t step = hand.cardCount == 1 ? fullWidth
                                           : std::min<int16_t>(fullWidth + theme.spaceSm,
                                                               (area.width - fullWidth) / (hand.cardCount - 1));
  const bool fan =
      fullHeight >= screen.target().lineHeight(indexStyle.font) * 3 && (hand.cardCount == 1 || step >= exposed);
  const int16_t compactWidth = exposed + screen.target().lineHeight(indexStyle.font) / 2;
  const int columns = fan ? hand.cardCount : std::min<int>(hand.cardCount, std::max<int>(2, area.width / compactWidth));
  const int rows = (hand.cardCount + columns - 1) / columns;
  const int16_t height = fan ? fullHeight : (area.height - theme.spaceSm * (rows - 1)) / rows;
  const int16_t width = fan ? fullWidth : (area.width - theme.spaceSm * (columns - 1)) / columns;
  const int16_t stride = fan ? step : width + theme.spaceSm;
  for (int i = 0; i < hand.cardCount; ++i) {
    const int count = std::min<int>(columns, hand.cardCount - (i / columns) * columns);
    const int16_t left = area.x + (area.width - width - (count - 1) * stride) / 2;
    ui_casino_table::card(
        screen.target(),
        fui::makeRect(left + (i % columns) * stride, area.y + (i / columns) * (height + theme.spaceSm), width, height),
        theme, BlackjackGame::rank(hand.cards[i]), BlackjackGame::suit(hand.cards[i]), hidden && i == 1);
  }
}

void CasinoActivity::drawHands(UiScreen& screen, fui::Rect area) {
  const auto& state = store.game().state();
  const auto& theme = screen.theme();
  const int handIndex =
      state.phase == Phase::Settled ? std::clamp<int>(displayedHand, 0, state.handCount - 1) : state.activeHand;
  const bool wide = area.width > area.height * 3 / 2;
  if (wide) {
    const int16_t width = (area.width - theme.spaceLg * 2) / 2;
    drawHand(screen, fui::Rect{area.x, area.y, width, area.height}, state.dealer, true);
    drawHand(screen, fui::Rect{static_cast<int16_t>(area.right() - width), area.y, width, area.height},
             state.hands[handIndex], false);
  } else {
    const int16_t height = (area.height - theme.spaceLg) / 2;
    drawHand(screen, fui::Rect{area.x, area.y, area.width, height}, state.dealer, true);
    drawHand(screen, fui::Rect{area.x, static_cast<int16_t>(area.y + height + theme.spaceLg), area.width, height},
             state.hands[handIndex], false);
  }
}

void CasinoActivity::buildRound(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& state = store.game().state();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const int16_t row = screen.contentRect().height < theme.rowHeight * 7 ? theme.minTouchSize : theme.rowHeight;
  const int16_t gap = theme.spaceSm;
  char text[80];
  char money[48];
  if (state.phase == Phase::Settled) {
    const auto next = screen.takeBottom(row, gap);
    drawButton(screen, next, tr(STR_CASINO_AGAIN), AGAIN, true);
    const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
    const auto result = screen.takeBottom(titleLine + line + theme.spaceMd * 2, gap);
    ui_casino_table::frame(screen.target(), result, theme);
    const char* label = state.handCount == 1 ? resultLabel(state.hands[0].result) : tr(STR_CASINO_ROUND_COMPLETE);
    drawLabel(
        screen,
        fui::makeRect(result.x + theme.spaceMd, result.y + theme.spaceSm, result.width - theme.spaceMd * 2, titleLine),
        label, true, fui::TextAlign::Center);
    netMoney(text, sizeof(text), state.roundReturnCents - state.roundWagerCents);
    drawLabel(screen,
              fui::makeRect(result.x + theme.spaceMd, result.y + titleLine + theme.spaceSm,
                            result.width - theme.spaceMd * 2, line),
              text, false, fui::TextAlign::Center);
    if (state.insuranceCents) {
      netMoney(money, sizeof(money), state.insuranceReturnCents - state.insuranceCents);
      snprintf(text, sizeof(text), tr(STR_CASINO_INSURANCE_RESULT), money);
      drawLabel(screen, screen.takeBottom(line, gap), text, false, fui::TextAlign::Center);
    }
  } else if (state.phase == Phase::Insurance) {
    const auto buttons = screen.takeBottom(row, gap);
    const int16_t width = (buttons.width - theme.spaceMd) / 2;
    ui_casino::money(money, sizeof(money), state.hands[0].wagerCents / 2);
    snprintf(text, sizeof(text), tr(STR_CASINO_INSURE_AMOUNT), money);
    drawButton(screen, fui::Rect{buttons.x, buttons.y, width, row}, text, INSURE, false, store.game().canInsure());
    drawButton(screen, fui::Rect{static_cast<int16_t>(buttons.right() - width), buttons.y, width, row},
               tr(STR_CASINO_DECLINE), DECLINE, true);
    drawLabel(screen, screen.takeBottom(screen.target().lineHeight(theme.titleText.font), gap),
              tr(STR_CASINO_INSURANCE), true, fui::TextAlign::Center);
  } else {
    const auto surrender = screen.takeBottom(theme.minTouchSize, gap);
    const auto secondary = screen.takeBottom(row, gap);
    const auto primary = screen.takeBottom(row, gap);
    const int16_t width = (primary.width - theme.spaceMd) / 2;
    drawButton(screen, fui::Rect{primary.x, primary.y, width, row}, tr(STR_CASINO_HIT), HIT, true,
               store.game().canHit());
    drawButton(screen, fui::Rect{static_cast<int16_t>(primary.right() - width), primary.y, width, row},
               tr(STR_CASINO_STAND), STAND, false, store.game().canStand());
    drawButton(screen, fui::Rect{secondary.x, secondary.y, width, row}, tr(STR_CASINO_DOUBLE), DOUBLE, false,
               store.game().canDouble());
    drawButton(screen, fui::Rect{static_cast<int16_t>(secondary.right() - width), secondary.y, width, row},
               tr(STR_CASINO_SPLIT), SPLIT, false, store.game().canSplit());
    drawButton(screen, surrender, tr(STR_CASINO_SURRENDER), SURRENDER, false, store.game().canSurrender(), false);
  }
  if (state.handCount > 1) {
    const auto tabs = screen.takeTop(theme.minTouchSize, gap);
    const int16_t width = (tabs.width - gap * (state.handCount - 1)) / state.handCount;
    for (int i = 0; i < state.handCount; ++i) {
      snprintf(text, sizeof(text), tr(STR_CASINO_HAND), static_cast<unsigned>(i + 1));
      drawButton(screen, fui::Rect{static_cast<int16_t>(tabs.x + i * (width + gap)), tabs.y, width, tabs.height}, text,
                 HAND_BASE + i, i == (state.phase == Phase::Settled ? displayedHand : state.activeHand),
                 state.phase == Phase::Settled);
    }
  }
  const int index =
      state.phase == Phase::Settled ? std::clamp<int>(displayedHand, 0, state.handCount - 1) : state.activeHand;
  ui_casino::money(money, sizeof(money), state.hands[index].wagerCents);
  snprintf(text, sizeof(text), tr(STR_CASINO_BET_AMOUNT), money);
  const auto wager = screen.takeTop(line, gap);
  drawLabel(screen, wager, text, false, fui::TextAlign::Center);
  if (state.phase == Phase::Settled && state.handCount > 1) {
    netMoney(money, sizeof(money), state.hands[index].returnCents - state.hands[index].wagerCents);
    snprintf(text, sizeof(text), tr(STR_CASINO_HAND_RESULT), resultLabel(state.hands[index].result), money);
    drawLabel(screen, screen.takeTop(line, gap), text, false, fui::TextAlign::Center);
  }
  drawHands(screen, screen.contentRect());
}

void CasinoActivity::buildBetEntry(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t gap = theme.spaceSm;
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const bool wide = screen.contentRect().width > screen.contentRect().height;
  fui::Rect readout;
  fui::Rect keypad;
  fui::Rect footer;
  int16_t row = theme.rowHeight;
  if (wide) {
    const auto area = screen.contentRect();
    const int16_t leftWidth = area.width * 2 / 5;
    readout = fui::Rect{area.x, area.y, static_cast<int16_t>(leftWidth - theme.spaceLg), area.height};
    const int16_t rightX = area.x + leftWidth;
    row = std::min<int16_t>(row, (area.height - gap * 3 - theme.spaceMd) / 5);
    keypad =
        fui::Rect{rightX, area.y, static_cast<int16_t>(area.right() - rightX), static_cast<int16_t>(row * 4 + gap * 3)};
    footer = fui::Rect{rightX, static_cast<int16_t>(keypad.bottom() + theme.spaceMd), keypad.width, row};
  } else {
    footer = screen.takeBottom(row, theme.spaceMd);
    keypad = screen.takeBottom(row * 4 + gap * 3, theme.spaceMd);
    readout = screen.contentRect();
  }
  drawLabel(screen, fui::Rect{readout.x, readout.y, readout.width, line}, tr(STR_CASINO_CUSTOM), true,
            fui::TextAlign::Center);
  auto small = theme.smallText;
  small.align = fui::TextAlign::Center;
  small.maxLines = wide ? 2 : 1;
  const int16_t hintHeight = line * small.maxLines;
  screen.target().text(
      fui::Rect{readout.x, static_cast<int16_t>(readout.bottom() - hintHeight), readout.width, hintHeight},
      tr(STR_CASINO_AMOUNT_HINT), small);
  ui_casino::amount(screen.target(),
                    fui::Rect{readout.x, static_cast<int16_t>(readout.y + line + gap), readout.width,
                              static_cast<int16_t>(readout.height - line - hintHeight - gap * 2)},
                    theme, enteredBet());
  const int16_t width = (keypad.width - gap * 2) / 3;
  for (int i = 0; i < 12; ++i) {
    const int digit = i == 10 ? 0 : i + 1;
    const fui::Rect cell{static_cast<int16_t>(keypad.x + (i % 3) * (width + gap)),
                         static_cast<int16_t>(keypad.y + (i / 3) * (row + gap)), width, row};
    char label[4];
    snprintf(label, sizeof(label), "%d", digit);
    drawButton(screen, cell,
               i == 9    ? tr(STR_CANCEL)
               : i == 11 ? tr(STR_CASINO_DELETE_DIGIT)
                         : label,
               i == 9    ? CLOSE
               : i == 11 ? ERASE
                         : DIGIT_BASE + digit);
  }
  drawButton(screen, footer, tr(STR_CASINO_SET_BET), ACCEPT_BET, canPlaceBet(enteredBet()), canPlaceBet(enteredBet()));
}

void CasinoActivity::buildRules(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  drawLabel(screen, screen.takeTop(screen.target().lineHeight(theme.titleText.font), theme.spaceMd),
            rulesGame == Game::Farkle && rulesPage == 0  ? tr(STR_FARKLE_SCORING)
            : rulesGame == Game::Slots && rulesPage == 0 ? tr(STR_SLOTS_PAYTABLE)
                                                         : tr(STR_CASINO_RULES),
            true);
  if (returnView == View::Lobby) {
    const auto tabs = screen.takeTop(theme.minTouchSize * 2 + theme.spaceSm, theme.spaceMd);
    const int16_t width = (tabs.width - theme.spaceSm * 2) / 3;
    static constexpr StrId NAMES[] = {StrId::STR_CASINO_SLOTS, StrId::STR_CASINO_BLACKJACK, StrId::STR_CASINO_ROULETTE,
                                      StrId::STR_CASINO_BACCARAT, StrId::STR_CASINO_FARKLE};
    static constexpr int CONTROLS[] = {RULES_SLOTS, RULES_BLACKJACK, RULES_ROULETTE, RULES_BACCARAT, RULES_FARKLE};
    static constexpr Game GAMES[] = {Game::Slots, Game::Blackjack, Game::Roulette, Game::Baccarat, Game::Farkle};
    for (int i = 0; i < 5; ++i)
      drawButton(screen,
                 fui::Rect{static_cast<int16_t>(tabs.x + (i % 3) * (width + theme.spaceSm)),
                           static_cast<int16_t>(tabs.y + (i / 3) * (theme.minTouchSize + theme.spaceSm)), width,
                           theme.minTouchSize},
                 I18N.get(NAMES[i]), CONTROLS[i], rulesGame == GAMES[i]);
  }
  const auto footer = screen.takeBottom(theme.rowHeight, theme.spaceMd);
  const StrId* rules = rulesGame == Game::Farkle     ? FARKLE_RULE_TEXT
                       : rulesGame == Game::Slots    ? SLOTS_RULE_TEXT
                       : rulesGame == Game::Roulette ? ROULETTE_RULE_TEXT
                       : rulesGame == Game::Baccarat ? BACCARAT_RULE_TEXT
                                                     : RULE_TEXT;
  const int ruleCount = rulesGame == Game::Farkle     ? sizeof(FARKLE_RULE_TEXT) / sizeof(FARKLE_RULE_TEXT[0])
                        : rulesGame == Game::Slots    ? sizeof(SLOTS_RULE_TEXT) / sizeof(SLOTS_RULE_TEXT[0])
                        : rulesGame == Game::Roulette ? sizeof(ROULETTE_RULE_TEXT) / sizeof(ROULETTE_RULE_TEXT[0])
                        : rulesGame == Game::Baccarat ? sizeof(BACCARAT_RULE_TEXT) / sizeof(BACCARAT_RULE_TEXT[0])
                                                      : sizeof(RULE_TEXT) / sizeof(RULE_TEXT[0]);
  // Page each rule in a measured band so translated text remains fully readable.
  auto text = theme.bodyText;
  text.maxLines = 20;
  int maxHeight = 0;
  for (int i = 0; i < ruleCount; ++i)
    maxHeight = std::max<int>(
        maxHeight,
        fui::measureWrappedText(screen.target(), I18N.get(rules[i]), text, screen.contentRect().width).height);
  const int16_t height =
      std::max<int>(line, maxHeight + (rulesGame != Game::Blackjack ? theme.spaceLg * 2 : theme.spaceMd));
  rulesPerPage = std::max<int>(1, screen.contentRect().height / height);
  const int firstTextPage = rulesGame == Game::Slots || rulesGame == Game::Farkle ? 1 : 0;
  const int pages = firstTextPage + (ruleCount + rulesPerPage - 1) / rulesPerPage;
  rulesPage = std::clamp(rulesPage, 0, pages - 1);
  if (firstTextPage && rulesPage == 0) {
    if (rulesGame == Game::Farkle)
      buildFarklePaytable(screen);
    else
      buildSlotsPaytable(screen);
  } else {
    const int page = rulesPage - firstTextPage;
    for (int i = page * rulesPerPage; i < std::min(ruleCount, (page + 1) * rulesPerPage); ++i)
      screen.target().text(screen.takeTop(height), I18N.get(rules[i]), text);
  }
  const int16_t width = (footer.width - theme.spaceSm * 2) / 3;
  drawButton(screen, fui::Rect{footer.x, footer.y, width, footer.height}, tr(STR_TODO_PREVIOUS), PREVIOUS_RULE, false,
             rulesPage > 0);
  drawButton(screen, fui::Rect{static_cast<int16_t>(footer.x + width + theme.spaceSm), footer.y, width, footer.height},
             tr(STR_DONE), CLOSE, true);
  drawButton(screen, fui::Rect{static_cast<int16_t>(footer.right() - width), footer.y, width, footer.height},
             tr(STR_TODO_NEXT), NEXT_RULE, false, rulesPage + 1 < pages);
}

void CasinoActivity::buildStorageError(UiScreen& screen) {
  const auto& theme = screen.theme();
  drawButton(screen, screen.takeBottom(theme.rowHeight, theme.spaceLg), tr(STR_RETRY), RETRY, true);
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  screen.spacer(std::max<int>(0, (screen.contentRect().height - line * 7) / 2));
  drawLabel(screen, screen.takeTop(line * 2, theme.spaceLg),
            loadFailed ? tr(STR_CASINO_LOAD_ERROR) : tr(STR_CASINO_SAVE_ERROR), true, fui::TextAlign::Center);
  drawLabel(screen, screen.takeTop(line * 2, theme.spaceLg), tr(STR_CASINO_CHECK_SD), false, fui::TextAlign::Center);
  if (dirty) drawLabel(screen, screen.takeTop(line * 3), tr(STR_CASINO_SAVE_HINT), false, fui::TextAlign::Center);
}

void CasinoActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  const bool clean = refreshPolicy.nextFrameNeedsCleaning();
  LOG_DBG("CASINO", "Display refresh: %s", clean ? "clean" : "fast");
  renderer.displayBuffer(clean ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
