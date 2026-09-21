#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CasinoActivity.h"
#include "components/UiCasino.h"
#include "components/UiFarkle.h"

namespace fui = freeink::ui;
namespace {
using Phase = FarkleGame::Phase;
using Player = FarkleGame::Player;
constexpr uint32_t TARGETS[] = {1500, 2000, 4000};

void points(char* text, size_t size, uint32_t value) { snprintf(text, size, "%lu", static_cast<unsigned long>(value)); }
}  // namespace

int CasinoActivity::farklePrimaryControl() const {
  const auto& state = store.farkle().state();
  switch (state.phase) {
    case Phase::Betting:
      return F_START;
    case Phase::AwaitRoll:
      return F_ROLL;
    case Phase::TurnEnded:
      return F_CONTINUE;
    case Phase::Settled:
      return F_AGAIN;
    case Phase::Selecting:
      if (state.activePlayer == Player::Opponent) return F_CONTINUE;
      for (unsigned i = 0; i < FarkleGame::DICE; ++i)
        if (state.rolledMask & (1 << i)) return F_DIE_BASE + i;
  }
  return RULES;
}

void CasinoActivity::activateFarkle(int control) {
  auto& game = store.farkle();
  const auto& state = game.state();
  bool changed = false;
  if (state.phase == Phase::Betting) {
    if (control >= F_TARGET_BASE && control < F_TARGET_BASE + 3) {
      farkleTarget = TARGETS[control - F_TARGET_BASE];
    } else if (control >= PRESET_BASE && control < PRESET_BASE + 4 && canPlaceBet(PRESETS[control - PRESET_BASE])) {
      betCents = PRESETS[control - PRESET_BASE];
    } else if (control == CUSTOM) {
      betInput[0] = '\0';
      view = View::BetEntry;
    } else if (control == MAX_BET) {
      betCents = store.game().state().balanceCents / 100 * 100;
      normalizeBet();
    } else if (control == F_START) {
      changed = store.startFarkle(betCents, farkleTarget);
    }
  } else if (state.phase == Phase::Settled) {
    if (control == F_AGAIN) {
      betCents = state.wagerCents;
      farkleTarget = state.targetScore;
      changed = game.nextMatch();
      normalizeBet();
    }
  } else if (state.phase == Phase::TurnEnded) {
    if (control == F_CONTINUE) changed = store.advanceFarkleTurn();
  } else if (state.phase == Phase::AwaitRoll) {
    if (control == F_ROLL) changed = store.rollFarkle(&CasinoActivity::randomWord);
  } else if (state.activePlayer == Player::You) {
    if (control >= F_DIE_BASE && control < F_DIE_BASE + static_cast<int>(FarkleGame::DICE)) {
      const uint8_t bit = 1 << (control - F_DIE_BASE);
      if (state.rolledMask & bit) farkleSelection ^= bit;
    } else if (control == F_BANK) {
      changed = store.bankFarkle(farkleSelection);
    } else if (control == F_ROLL) {
      changed = store.continueFarkle(farkleSelection, &CasinoActivity::randomWord);
    }
  } else if (control == F_CONTINUE) {
    // Expose each opponent throw and decision before advancing; e-ink needs no animation loop.
    const uint8_t selection = game.bestSelection();
    changed = game.computerShouldBank(selection) ? store.bankFarkle(selection)
                                                 : store.continueFarkle(selection, &CasinoActivity::randomWord);
  }
  if (changed) {
    farkleSelection = 0;
    selectedControl = farklePrimaryControl();
    saveChanges();
  }
}

void CasinoActivity::buildFarkleBetting(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  const bool compact = screen.contentRect().height < theme.rowHeight * 7;
  const int16_t row = compact ? theme.minTouchSize : theme.rowHeight;
  const int16_t gap = compact ? theme.spaceSm : theme.spaceMd;
  const auto start = screen.takeBottom(row, gap);
  const auto custom = screen.takeBottom(row, gap);
  const auto presets = screen.takeBottom(row, gap);
  const auto targets = screen.takeBottom(row, gap);
  auto caption = theme.smallText;
  caption.align = fui::TextAlign::Center;
  screen.target().text(screen.takeBottom(smallLine, theme.spaceSm), tr(STR_FARKLE_TARGET), caption);
  const bool funded = store.game().state().balanceCents >= FarkleGame::MIN_BET_CENTS;
  drawButton(screen, start, tr(STR_FARKLE_PLAY), F_START, canPlaceBet(betCents), canPlaceBet(betCents));
  const int16_t half = (custom.width - gap) / 2;
  drawButton(screen, fui::Rect{custom.x, custom.y, half, row}, tr(STR_CASINO_CUSTOM), CUSTOM, false, funded);
  drawButton(screen, fui::Rect{static_cast<int16_t>(custom.right() - half), custom.y, half, row}, tr(STR_CASINO_MAX),
             MAX_BET, false, funded);
  for (int i = 0; i < 4; ++i) {
    const int16_t width = (presets.width - theme.spaceSm * 3) / 4;
    char text[24];
    ui_casino::money(text, sizeof(text), PRESETS[i]);
    const bool enabled = canPlaceBet(PRESETS[i]);
    drawButton(screen, fui::Rect{static_cast<int16_t>(presets.x + i * (width + theme.spaceSm)), presets.y, width, row},
               text, PRESET_BASE + i, enabled && betCents == PRESETS[i], enabled);
  }
  for (int i = 0; i < 3; ++i) {
    const int16_t width = (targets.width - theme.spaceSm * 2) / 3;
    char text[16];
    points(text, sizeof(text), TARGETS[i]);
    drawButton(screen, fui::Rect{static_cast<int16_t>(targets.x + i * (width + theme.spaceSm)), targets.y, width, row},
               text, F_TARGET_BASE + i, farkleTarget == TARGETS[i]);
  }
  const auto area = screen.contentRect();
  if (!funded) {
    drawLabel(screen, area, tr(STR_CASINO_NO_FUNDS), false, fui::TextAlign::Center);
    return;
  }
  if (area.height >= line * 5) {
    const int16_t side = std::min<int16_t>(theme.rowHeight, area.height / 4);
    const int16_t width = side * 3 + theme.spaceMd * 2;
    screen.spacer(std::max<int>(0, (area.height - side - line * 3 - theme.spaceLg * 2) / 2));
    const auto motif = screen.takeTop(side, theme.spaceLg);
    for (int i = 0; i < 3; ++i)
      ui_farkle::die(screen.target(),
                     fui::Rect{static_cast<int16_t>(motif.x + (motif.width - width) / 2 + i * (side + theme.spaceMd)),
                               motif.y, side, side},
                     fui::Paint::solid(theme.bodyText.color), i + 3);
  }
  if (screen.contentRect().height >= line * 2)
    drawLabel(screen, screen.takeTop(line, theme.spaceSm), tr(STR_CASINO_BET), false, fui::TextAlign::Center);
  const auto amount = screen.contentRect();
  // Use the proportional balance digits at their native size for an understated wager.
  char text[48];
  ui_casino::money(text, sizeof(text), betCents);
  const auto style = theme.titleText;
  const int16_t width = screen.target().measureText(style.font, text, style).width;
  if (compact)
    drawLabel(screen, amount, text, true, fui::TextAlign::Center);
  else {
    const int16_t height = std::min<int16_t>(theme.rowHeight, amount.height);
    const int16_t naturalWidth = std::min<int16_t>(amount.width, std::max<int16_t>(width * 2, theme.rowHeight * 2));
    ui_casino::balance(
        screen.target(),
        fui::Rect{static_cast<int16_t>(amount.x + (amount.width - naturalWidth) / 2), amount.y, naturalWidth, height},
        theme, betCents);
  }
}

void CasinoActivity::drawFarkleDice(UiScreen& screen, fui::Rect area) {
  const auto& theme = screen.theme();
  const auto& game = store.farkle();
  const auto& state = game.state();
  const bool selecting = state.phase == Phase::Selecting;
  const bool human = state.activePlayer == Player::You;
  const uint8_t selected = selecting ? (human ? farkleSelection : game.bestSelection()) : 0;
  const int columns = renderer.getScreenWidth() > renderer.getScreenHeight() ? 6 : 3;
  const int rows = 6 / columns;
  const int16_t labelHeight = screen.target().lineHeight(theme.smallText.font);
  const int16_t gap = theme.spaceMd;
  const int16_t cellWidth = (area.width - gap * (columns - 1)) / columns;
  const int16_t cellHeight = (area.height - gap * (rows - 1)) / rows;
  const int16_t side = std::min<int16_t>(cellWidth, cellHeight - labelHeight - theme.spaceSm);
  if (side < 6) return;
  const int16_t gridHeight = rows * (side + labelHeight + theme.spaceSm) + (rows - 1) * gap;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  for (unsigned i = 0; i < FarkleGame::DICE; ++i) {
    const uint8_t bit = 1 << i;
    const bool held = state.heldMask & bit;
    const bool chosen = selected & bit;
    const fui::Rect box{static_cast<int16_t>(area.x + (i % columns) * (cellWidth + gap) + (cellWidth - side) / 2),
                        static_cast<int16_t>(area.y + (area.height - gridHeight) / 2 +
                                             (i / columns) * (side + labelHeight + theme.spaceSm + gap)),
                        side, side};
    auto foreground = ink;
    if (selecting && human && (state.rolledMask & bit)) {
      drawButton(screen, box, nullptr, F_DIE_BASE + i, chosen);
      const auto visual = screen.frame().stateFor(ACTION_CONTROL, F_DIE_BASE + i, buttonProps.state);
      foreground = buttonProps.styles.resolve(visual).foreground;
    } else {
      if (chosen) {
        screen.target().fill(box, ink);
        foreground = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
      } else
        screen.target().stroke(box, ink, 1);
    }
    ui_farkle::die(screen.target(), box.inset(fui::Insets{theme.spaceSm, theme.spaceSm, theme.spaceSm, theme.spaceSm}),
                   foreground, state.dice[i], false);
    if (selecting && human && chosen && buttonNavigation && selectedControl == F_DIE_BASE + static_cast<int>(i))
      screen.target().stroke(box.inset(fui::Insets{theme.spaceSm, theme.spaceSm, theme.spaceSm, theme.spaceSm}),
                             foreground, 1);
    if (held) ui_casino::soften(screen.target(), box, theme);
    auto caption = theme.smallText;
    caption.align = fui::TextAlign::Center;
    const char* label = held ? tr(STR_FARKLE_HELD) : chosen ? tr(STR_FARKLE_KEEP) : nullptr;
    if (label)
      screen.target().text(fui::Rect{static_cast<int16_t>(box.x - (cellWidth - side) / 2),
                                     static_cast<int16_t>(box.bottom() + theme.spaceSm), cellWidth, labelHeight},
                           label, caption);
  }
}

void CasinoActivity::buildFarkleRound(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& game = store.farkle();
  const auto& state = game.state();
  const bool compact = renderer.getScreenWidth() > renderer.getScreenHeight();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  const int16_t row = compact ? theme.minTouchSize : theme.rowHeight;
  const int16_t gap = compact ? theme.spaceSm : theme.spaceMd;
  const auto actions = screen.takeBottom(row, gap);
  const auto summary = screen.takeBottom(line * 2, gap);
  const bool selecting = state.phase == Phase::Selecting;
  const bool human = state.activePlayer == Player::You;
  const uint8_t selected = human ? farkleSelection : game.bestSelection();
  const uint32_t selectedPoints = selecting ? game.scoreSelection(selected) : 0;
  if (state.phase == Phase::Betting) return;
  if (state.phase == Phase::Settled)
    drawButton(screen, actions, tr(STR_FARKLE_AGAIN), F_AGAIN, true);
  else if (state.phase == Phase::TurnEnded)
    drawButton(screen, actions, human ? tr(STR_FARKLE_OPPONENT_TURN) : tr(STR_FARKLE_YOUR_TURN), F_CONTINUE, true);
  else if (state.phase == Phase::AwaitRoll)
    drawButton(screen, actions, human ? tr(STR_FARKLE_ROLL) : tr(STR_FARKLE_OPPONENT_ROLL), F_ROLL, true);
  else if (!human)
    drawButton(screen, actions, tr(STR_FARKLE_CONTINUE), F_CONTINUE, true);
  else {
    const int16_t half = (actions.width - gap) / 2;
    drawButton(screen, fui::Rect{actions.x, actions.y, half, row}, tr(STR_FARKLE_ROLL_AGAIN), F_ROLL, false,
               selectedPoints > 0);
    drawButton(screen, fui::Rect{static_cast<int16_t>(actions.right() - half), actions.y, half, row},
               tr(STR_FARKLE_BANK), F_BANK, selectedPoints > 0, selectedPoints > 0);
  }
  const auto scoreboard = screen.takeTop(line * 2 + theme.spaceMd * 2, gap);
  const int16_t half = (scoreboard.width - gap) / 2;
  for (int i = 0; i < 2; ++i) {
    const fui::Rect box{static_cast<int16_t>(scoreboard.x + i * (half + gap)), scoreboard.y, half, scoreboard.height};
    const bool active = i == static_cast<int>(state.activePlayer);
    screen.target().stroke(box, fui::Paint::solid(theme.bodyText.color), active ? 3 : 1);
    drawLabel(screen, fui::Rect{box.x, static_cast<int16_t>(box.y + theme.spaceSm), box.width, line},
              i == 0 ? tr(STR_CASINO_YOU) : tr(STR_FARKLE_OPPONENT), active, fui::TextAlign::Center);
    char text[16];
    points(text, sizeof(text), state.scores[i]);
    drawLabel(screen, fui::Rect{box.x, static_cast<int16_t>(box.y + line + theme.spaceSm), box.width, line}, text, true,
              fui::TextAlign::Center);
  }
  char money[40];
  char text[104];
  ui_casino::money(money, sizeof(money), state.wagerCents);
  snprintf(text, sizeof(text), tr(STR_FARKLE_MATCH_INFO), static_cast<unsigned long>(state.targetScore), money);
  auto small = theme.smallText;
  small.align = fui::TextAlign::Center;
  screen.target().text(screen.takeTop(smallLine, gap), text, small);
  const auto status = screen.takeTop(line, gap);
  const char* statusText =
      selecting ? (human ? tr(STR_FARKLE_SELECT) : tr(STR_FARKLE_OPPONENT_KEEP))
      : state.phase == Phase::TurnEnded
          ? (state.endReason == FarkleGame::EndReason::Bust ? tr(STR_FARKLE_BUST) : tr(STR_FARKLE_BANKED))
      : state.phase == Phase::Settled ? (human ? tr(STR_CASINO_WIN) : tr(STR_CASINO_LOSE))
      : human                         ? tr(STR_FARKLE_YOUR_TURN)
                                      : tr(STR_FARKLE_OPPONENT_TURN);
  drawLabel(screen, status, statusText, true, fui::TextAlign::Center);
  if (state.phase == Phase::Settled) {
    const int64_t net = state.returnCents - state.wagerCents;
    ui_casino::money(money, sizeof(money), net < 0 ? -net : net);
    snprintf(text, sizeof(text), net > 0 ? tr(STR_CASINO_PLUS) : tr(STR_CASINO_MINUS), money);
    drawLabel(screen, summary, text, true, fui::TextAlign::Center);
  } else if (selecting) {
    snprintf(text, sizeof(text), tr(STR_FARKLE_TURN_POINTS), static_cast<unsigned long>(state.turnPoints),
             static_cast<unsigned long>(selectedPoints));
    drawLabel(screen, fui::Rect{summary.x, summary.y, summary.width, line}, text, true, fui::TextAlign::Center);
    const char* hint = selected && !selectedPoints ? tr(STR_FARKLE_INVALID)
                       : !human
                           ? (game.computerShouldBank(selected) ? tr(STR_FARKLE_WILL_BANK) : tr(STR_FARKLE_WILL_ROLL))
                           : tr(STR_FARKLE_SELECTION_HINT);
    screen.target().text(fui::Rect{summary.x, static_cast<int16_t>(summary.y + line), summary.width, line}, hint,
                         small);
  } else if (state.phase == Phase::TurnEnded && state.endReason == FarkleGame::EndReason::Banked) {
    snprintf(text, sizeof(text), tr(STR_FARKLE_BANKED_POINTS), static_cast<unsigned long>(state.turnPoints));
    drawLabel(screen, summary, text, true, fui::TextAlign::Center);
  } else if (state.phase == Phase::TurnEnded) {
    drawLabel(screen, summary, tr(STR_FARKLE_BUST_HINT), false, fui::TextAlign::Center);
  }
  drawFarkleDice(screen, screen.contentRect());
}

void CasinoActivity::buildFarklePaytable(UiScreen& screen) {
  const auto& theme = screen.theme();
  static constexpr StrId LABELS[] = {
      StrId::STR_FARKLE_SCORE_ONE,         StrId::STR_FARKLE_SCORE_FIVE,         StrId::STR_FARKLE_SCORE_TRIPLE_ONE,
      StrId::STR_FARKLE_SCORE_TRIPLE_TWO,  StrId::STR_FARKLE_SCORE_TRIPLE_THREE, StrId::STR_FARKLE_SCORE_TRIPLE_FOUR,
      StrId::STR_FARKLE_SCORE_TRIPLE_FIVE, StrId::STR_FARKLE_SCORE_TRIPLE_SIX,   StrId::STR_FARKLE_SCORE_FOUR,
      StrId::STR_FARKLE_SCORE_FIVE_KIND,   StrId::STR_FARKLE_SCORE_SIX,          StrId::STR_FARKLE_SCORE_LOW,
      StrId::STR_FARKLE_SCORE_HIGH,        StrId::STR_FARKLE_SCORE_STRAIGHT};
  static constexpr uint16_t SCORES[] = {100, 50, 1000, 200, 300, 400, 500, 600, 2, 4, 8, 500, 750, 1500};
  const auto area = screen.contentRect();
  const int16_t gap = theme.spaceLg;
  const int16_t width = (area.width - gap) / 2;
  const int16_t row = area.height / 7;
  auto label = theme.smallText;
  label.maxLines = 2;
  auto amount = theme.smallText;
  amount.bold = true;
  amount.align = fui::TextAlign::Right;
  for (int i = 0; i < 14; ++i) {
    const fui::Rect cell{static_cast<int16_t>(area.x + (i / 7) * (width + gap)),
                         static_cast<int16_t>(area.y + (i % 7) * row), width, row};
    char text[24];
    if (i >= 8 && i <= 10)
      snprintf(text, sizeof(text), tr(STR_FARKLE_TRIPLE_MULTIPLIER), static_cast<unsigned>(SCORES[i]));
    else
      points(text, sizeof(text), SCORES[i]);
    const int16_t scoreWidth = screen.target().measureText(amount.font, text, amount).width + theme.spaceSm;
    screen.target().text(fui::Rect{cell.x, cell.y, static_cast<int16_t>(cell.width - scoreWidth), cell.height},
                         I18N.get(LABELS[i]), label);
    screen.target().text(fui::Rect{static_cast<int16_t>(cell.right() - scoreWidth), cell.y, scoreWidth, cell.height},
                         text, amount);
    screen.target().line(fui::Point{cell.x, cell.bottom()}, fui::Point{cell.right(), cell.bottom()}, 1,
                         fui::Paint::dither(fui::Color::LightGray));
  }
}
