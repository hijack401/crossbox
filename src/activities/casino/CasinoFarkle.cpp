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
      if (changed) refreshPolicy.allowCleaning();
      normalizeBet();
    }
  } else if (state.phase == Phase::TurnEnded) {
    if (control == F_CONTINUE) changed = store.advanceFarkleTurn();
    if (changed) refreshPolicy.allowCleaning();
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

void CasinoActivity::buildFarkleTable(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  const bool compact = screen.contentRect().height < theme.rowHeight * 7;
  const auto header = screen.takeTop(compact ? titleLine : titleLine + smallLine, theme.spaceMd);
  const int16_t scoringWidth = header.width / 3;
  auto title = theme.titleText;
  title.bold = true;
  screen.target().text(fui::makeRect(header.x, header.y, header.width - scoringWidth, titleLine), tr(STR_CASINO_FARKLE),
                       title);
  drawButton(screen, fui::makeRect(header.right() - scoringWidth, header.y, scoringWidth, header.height),
             tr(STR_FARKLE_SCORING), RULES, false, true, false);
  if (!compact) {
    char amount[48];
    char label[80];
    ui_casino::money(amount, sizeof(amount), store.game().state().balanceCents);
    snprintf(label, sizeof(label), tr(STR_CASINO_BALANCE_AMOUNT), amount);
    screen.target().text(fui::makeRect(header.x, header.y + titleLine, header.width - scoringWidth, smallLine), label,
                         theme.smallText);
  }
  ui_farkle::ornament(screen.target(), screen.takeTop(theme.spaceMd, theme.spaceMd), theme);
  if (store.farkle().state().phase == Phase::Betting)
    buildFarkleBetting(screen);
  else
    buildFarkleRound(screen);
}

void CasinoActivity::buildFarkleBetting(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
  const bool compact = screen.contentRect().height < theme.rowHeight * 7;
  const int16_t row = compact ? theme.minTouchSize : theme.rowHeight;
  const int16_t gap = compact ? theme.spaceSm : theme.spaceMd;
  const auto start = screen.takeBottom(row, gap);
  const auto custom = screen.takeBottom(theme.minTouchSize, gap);
  const auto presets = screen.takeBottom(row, gap);
  auto caption = theme.smallText;
  caption.align = fui::TextAlign::Center;
  screen.target().text(screen.takeTop(smallLine, theme.spaceSm), tr(STR_FARKLE_TARGET), caption);
  const auto targets = screen.takeTop(row, gap);
  const bool funded = store.game().state().balanceCents >= FarkleGame::MIN_BET_CENTS;
  drawButton(screen, start, tr(STR_FARKLE_PLAY), F_START, canPlaceBet(betCents), canPlaceBet(betCents));
  const int16_t half = (custom.width - gap) / 2;
  drawButton(screen, fui::makeRect(custom.x, custom.y, half, custom.height), tr(STR_CASINO_CUSTOM), CUSTOM, false,
             funded);
  drawButton(screen, fui::makeRect(custom.right() - half, custom.y, half, custom.height), tr(STR_CASINO_MAX), MAX_BET,
             false, funded);
  for (int i = 0; i < 4; ++i) {
    const int16_t width = (presets.width - theme.spaceSm * 3) / 4;
    char text[24];
    ui_casino::money(text, sizeof(text), PRESETS[i]);
    const bool enabled = canPlaceBet(PRESETS[i]);
    drawButton(screen, fui::makeRect(presets.x + i * (width + theme.spaceSm), presets.y, width, row), text,
               PRESET_BASE + i, enabled && betCents == PRESETS[i], enabled);
  }
  for (int i = 0; i < 3; ++i) {
    const int16_t width = (targets.width - theme.spaceSm * 2) / 3;
    char text[16];
    points(text, sizeof(text), TARGETS[i]);
    drawButton(screen, fui::makeRect(targets.x + i * (width + theme.spaceSm), targets.y, width, row), text,
               F_TARGET_BASE + i, farkleTarget == TARGETS[i]);
  }
  const auto area = screen.contentRect();
  if (!funded) {
    drawLabel(screen, area, tr(STR_CASINO_NO_FUNDS), false, fui::TextAlign::Center);
    return;
  }
  const int16_t amountHeight = std::min<int16_t>(theme.rowHeight, area.height / 3);
  const int16_t motifHeight =
      std::min<int16_t>(theme.rowHeight + theme.spaceLg * 2, area.height - smallLine - amountHeight - gap * 2);
  screen.spacer(std::max<int16_t>(0, (area.height - motifHeight - smallLine - amountHeight - gap * 2) / 2));
  if (motifHeight >= theme.minTouchSize) {
    const auto motif = screen.takeTop(motifHeight, gap);
    const int16_t side =
        std::min<int>({motif.height - gap, (motif.width - theme.spaceLg * 4) / 3, theme.rowHeight + theme.spaceLg});
    const int16_t width = side * 3 + theme.spaceSm * 2;
    static constexpr uint8_t VALUES[] = {5, 1, 3};
    for (int i = 0; i < 3; ++i) {
      const int16_t y = motif.y + (motif.height - side) / 2 + (i == 1 ? -theme.spaceSm : theme.spaceSm);
      ui_farkle::carvedDie(
          screen.target(),
          fui::makeRect(motif.x + (motif.width - width) / 2 + i * (side + theme.spaceSm), y, side, side), theme,
          VALUES[i]);
    }
  }
  screen.target().text(screen.takeTop(smallLine), tr(STR_FARKLE_WAGER), caption);
  char amount[48];
  ui_casino::money(amount, sizeof(amount), betCents);
  auto number = theme.titleText;
  number.align = fui::TextAlign::Center;
  screen.target().text(screen.takeTop(std::max<int16_t>(amountHeight, titleLine)), amount, number);
}

void CasinoActivity::drawFarkleScoreboard(UiScreen& screen, fui::Rect area) {
  const auto& theme = screen.theme();
  const auto& state = store.farkle().state();
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
  const int16_t gap = theme.spaceLg * 2;
  const int16_t half = (area.width - gap) / 2;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const int16_t trackHeight = theme.spaceSm;
  for (int i = 0; i < 2; ++i) {
    const auto box = fui::makeRect(area.x + i * (half + gap), area.y, half, area.height);
    const bool active = i == static_cast<int>(state.activePlayer);
    auto label = theme.smallText;
    label.bold = active;
    label.align = i ? fui::TextAlign::Right : fui::TextAlign::Left;
    screen.target().text(fui::makeRect(box.x, box.y, box.width, smallLine),
                         i == 0 ? tr(STR_CASINO_YOU) : tr(STR_FARKLE_OPPONENT), label);
    char text[16];
    points(text, sizeof(text), state.scores[i]);
    auto score = theme.titleText;
    score.bold = active;
    score.align = label.align;
    screen.target().text(fui::makeRect(box.x, box.y + smallLine, box.width, titleLine), text, score);
    const auto track = fui::makeRect(box.x, box.bottom() - trackHeight, box.width, trackHeight);
    screen.target().stroke(track, ink, 1);
    const int16_t progress =
        static_cast<uint64_t>(std::min(state.scores[i], state.targetScore)) * track.width / state.targetScore;
    if (progress) screen.target().fill(fui::makeRect(track.x, track.y, progress, track.height), ink);
  }
  const int16_t side = std::min<int16_t>(gap - theme.spaceSm, area.height / 2);
  ui_farkle::crest(screen.target(),
                   fui::makeRect(area.x + (area.width - side) / 2, area.y + (area.height - side) / 2, side, side),
                   theme);
}

void CasinoActivity::drawFarkleDice(UiScreen& screen, fui::Rect area) {
  const auto& theme = screen.theme();
  const auto& game = store.farkle();
  const auto& state = game.state();
  const bool selecting = state.phase == Phase::Selecting;
  const bool human = state.activePlayer == Player::You;
  const uint8_t selected = selecting ? (human ? farkleSelection : game.bestSelection()) : 0;
  const int columns = area.width > area.height * 2 ? 6 : 3;
  const int rows = 6 / columns;
  const int16_t labelHeight = screen.target().lineHeight(theme.smallText.font);
  const int16_t gap = theme.spaceMd;
  const int16_t cellWidth = (area.width - gap * (columns - 1)) / columns;
  const int16_t cellHeight = (area.height - gap * (rows - 1)) / rows;
  const int16_t side = std::min<int16_t>(cellWidth - theme.spaceSm, cellHeight - labelHeight - theme.spaceSm);
  if (side < 6) return;
  const int16_t gridHeight = rows * (side + labelHeight + theme.spaceSm) + (rows - 1) * gap;
  for (unsigned i = 0; i < FarkleGame::DICE; ++i) {
    const uint8_t bit = 1 << i;
    const bool held = state.heldMask & bit;
    const bool chosen = selected & bit;
    const auto box = fui::makeRect(
        area.x + (i % columns) * (cellWidth + gap) + (cellWidth - side) / 2,
        area.y + (area.height - gridHeight) / 2 + (i / columns) * (side + labelHeight + theme.spaceSm + gap), side,
        side);
    const bool interactive = selecting && human && (state.rolledMask & bit);
    if (interactive) {
      const int16_t control = F_DIE_BASE + i;
      const int16_t touchWidth = std::min<int16_t>(cellWidth, std::max<int16_t>(side, theme.minTouchSize));
      const int16_t touchHeight = std::min<int16_t>(cellHeight, std::max<int16_t>(side, theme.minTouchSize));
      screen.frame().hit(fui::makeRect(box.x - (touchWidth - side) / 2, box.y, touchWidth, touchHeight), ACTION_CONTROL,
                         control, fui::InputTouch);
      if (focusCount < 24) focusTargets[focusCount++] = control;
    }
    ui_farkle::carvedDie(screen.target(), box, theme, state.dice[i], chosen, held,
                         interactive && buttonNavigation && selectedControl == F_DIE_BASE + static_cast<int>(i));
    auto caption = theme.smallText;
    caption.align = fui::TextAlign::Center;
    const char* label = held ? tr(STR_FARKLE_HELD) : chosen ? tr(STR_FARKLE_KEEP) : nullptr;
    if (label)
      screen.target().text(
          fui::makeRect(box.x - (cellWidth - side) / 2, box.bottom() + theme.spaceSm, cellWidth, labelHeight), label,
          caption);
  }
}

void CasinoActivity::drawFarkleOutcome(UiScreen& screen, fui::Rect area) {
  const auto& theme = screen.theme();
  const auto& state = store.farkle().state();
  const bool settled = state.phase == Phase::Settled;
  const bool human = state.activePlayer == Player::You;
  const bool bust = state.endReason == FarkleGame::EndReason::Bust;
  const bool compact = area.height < theme.rowHeight * 3;
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  auto center = theme.smallText;
  center.align = fui::TextAlign::Center;
  center.maxLines = 2;
  auto title = compact ? theme.bodyText : theme.titleText;
  title.align = fui::TextAlign::Center;
  title.bold = true;
  const int16_t titleLine = screen.target().lineHeight(title.font);
  const int16_t resultHeight = settled ? titleLine : smallLine * 2;
  const int16_t inset = theme.spaceLg;
  const int16_t diceSide =
      settled ? 0 : std::min<int16_t>(theme.rowHeight, (area.width - inset * 2 - theme.spaceSm * 5) / 6);
  const int16_t detailHeight = compact ? 0 : smallLine * 2;
  const int16_t baseHeight =
      titleLine + resultHeight + detailHeight + theme.spaceSm * 2 + (diceSide ? diceSide + theme.spaceMd : 0);
  const int16_t crestSide =
      compact ? 0
              : std::min<int16_t>(theme.rowHeight + theme.spaceLg * 2,
                                  std::max<int16_t>(0, area.height - baseHeight - inset * 2 - theme.spaceMd));
  const bool showCrest = crestSide >= theme.minTouchSize;
  const int16_t contentHeight = baseHeight + (showCrest ? crestSide + theme.spaceMd : 0);
  int16_t y = area.y + std::max<int16_t>(inset, (area.height - contentHeight) / 2);
  ui_farkle::tray(screen.target(), area, theme);
  if (showCrest) {
    ui_farkle::crest(screen.target(), fui::makeRect(area.x + (area.width - crestSide) / 2, y, crestSide, crestSide),
                     theme, settled ? human : !bust);
    y += crestSide + theme.spaceMd;
  }
  const char* headline = settled ? (human ? tr(STR_FARKLE_VICTORY) : tr(STR_FARKLE_DEFEAT))
                         : bust  ? tr(STR_FARKLE_FARKLE)
                                 : tr(STR_FARKLE_BANKED);
  screen.target().text(fui::makeRect(area.x + inset, y, area.width - inset * 2, titleLine), headline, title);
  y += titleLine + theme.spaceSm;
  char text[64];
  if (settled) {
    char money[40];
    const int64_t net = state.returnCents - state.wagerCents;
    ui_casino::money(money, sizeof(money), net < 0 ? -net : net);
    snprintf(text, sizeof(text), net > 0 ? tr(STR_CASINO_PLUS) : tr(STR_CASINO_MINUS), money);
  } else if (!bust)
    snprintf(text, sizeof(text), tr(STR_FARKLE_BANKED_POINTS), static_cast<unsigned long>(state.turnPoints));
  else
    snprintf(text, sizeof(text), "%s", tr(STR_FARKLE_BUST_HINT));
  auto result = settled ? title : center;
  result.bold = settled || !bust;
  screen.target().text(fui::makeRect(area.x + inset, y, area.width - inset * 2, resultHeight), text, result);
  y += resultHeight + theme.spaceSm;
  if (detailHeight) {
    const char* detail = settled ? (human ? tr(STR_FARKLE_VICTORY_HINT) : tr(STR_FARKLE_DEFEAT_HINT))
                         : bust  ? tr(STR_FARKLE_NEXT_THROW)
                                 : tr(STR_FARKLE_SAFE_POINTS);
    screen.target().text(fui::makeRect(area.x + inset, y, area.width - inset * 2, detailHeight), detail, center);
    y += detailHeight;
  }
  if (diceSide >= 14) {
    y += theme.spaceMd;
    const int16_t width = diceSide * 6 + theme.spaceSm * 5;
    for (unsigned i = 0; i < FarkleGame::DICE; ++i) {
      const bool scored = state.heldMask & (1 << i);
      const bool banked = state.endReason == FarkleGame::EndReason::Banked;
      ui_farkle::carvedDie(
          screen.target(),
          fui::makeRect(area.x + (area.width - width) / 2 + i * (diceSide + theme.spaceSm), y, diceSide, diceSide),
          theme, state.dice[i], banked && scored, banked ? !scored : scored);
    }
  }
}

void CasinoActivity::buildFarkleRound(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& game = store.farkle();
  const auto& state = game.state();
  const bool compact = screen.contentRect().height < theme.rowHeight * 7;
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
  const int16_t row = compact ? theme.minTouchSize : theme.rowHeight;
  const int16_t gap = compact ? theme.spaceSm : theme.spaceMd;
  const bool selecting = state.phase == Phase::Selecting;
  const bool human = state.activePlayer == Player::You;
  const bool outcome = state.phase == Phase::TurnEnded || state.phase == Phase::Settled;
  const uint8_t selected = selecting ? (human ? farkleSelection : game.bestSelection()) : 0;
  const uint32_t selectedPoints = selecting ? game.scoreSelection(selected) : 0;
  const bool hotDice = selecting && selectedPoints && (selected | state.heldMask) == FarkleGame::ALL_DICE_MASK;
  const auto actions = screen.takeBottom(row, gap);
  const auto hint = screen.takeBottom(smallLine * (compact ? 1 : 2), gap);
  fui::Rect summary{};
  if (selecting) summary = screen.takeBottom(titleLine + smallLine + theme.spaceSm, gap);
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
    drawButton(screen, fui::makeRect(actions.x, actions.y, half, row),
               hotDice ? tr(STR_FARKLE_ROLL_ALL) : tr(STR_FARKLE_ROLL_AGAIN), F_ROLL, false, selectedPoints > 0);
    char bank[48];
    snprintf(bank, sizeof(bank), tr(STR_FARKLE_BANK_AMOUNT),
             static_cast<unsigned long long>(state.turnPoints) + selectedPoints);
    drawButton(screen, fui::makeRect(actions.right() - half, actions.y, half, row),
               selectedPoints ? bank : tr(STR_FARKLE_BANK), F_BANK, selectedPoints > 0, selectedPoints > 0);
  }
  const auto scores = screen.takeTop(smallLine + titleLine + theme.spaceMd, gap);
  drawFarkleScoreboard(screen, scores);
  char money[40];
  char text[104];
  ui_casino::money(money, sizeof(money), state.wagerCents);
  snprintf(text, sizeof(text), tr(STR_FARKLE_MATCH_INFO), static_cast<unsigned long>(state.targetScore), money);
  auto small = theme.smallText;
  small.align = fui::TextAlign::Center;
  screen.target().text(screen.takeTop(smallLine, gap), text, small);
  if (outcome) {
    drawFarkleOutcome(screen, screen.contentRect());
    return;
  }
  const auto table = screen.contentRect();
  ui_farkle::tray(screen.target(), table, theme);
  const int16_t inset = theme.spaceLg;
  const int16_t headingHeight = compact ? smallLine : titleLine;
  const auto heading = fui::makeRect(table.x + inset, table.y + theme.spaceMd, table.width - inset * 2, headingHeight);
  auto turnTitle = compact ? theme.bodyText : theme.titleText;
  turnTitle.align = fui::TextAlign::Center;
  turnTitle.bold = true;
  const char* status = hotDice ? tr(STR_FARKLE_HOT_DICE)
                       : human ? tr(STR_FARKLE_YOUR_THROW)
                               : tr(STR_FARKLE_OPPONENT_THROW);
  screen.target().text(heading, status, turnTitle);
  const auto dice = fui::makeRect(table.x + inset, heading.bottom() + theme.spaceSm, table.width - inset * 2,
                                  table.bottom() - heading.bottom() - theme.spaceLg);
  drawFarkleDice(screen, dice);
  small.maxLines = compact ? 1 : 2;
  const char* hintText =
      state.phase == Phase::AwaitRoll ? (human ? tr(STR_FARKLE_READY_HINT) : tr(STR_FARKLE_OPPONENT_READY))
      : selected && !selectedPoints   ? tr(STR_FARKLE_INVALID)
      : !human  ? (game.computerShouldBank(selected) ? tr(STR_FARKLE_WILL_BANK) : tr(STR_FARKLE_WILL_ROLL))
      : hotDice ? tr(STR_FARKLE_HOT_HINT)
                : tr(STR_FARKLE_SELECTION_HINT);
  screen.target().text(hint, hintText, small);
  if (selecting) {
    const int16_t width = (summary.width - theme.spaceLg) / 2;
    for (int i = 0; i < 2; ++i) {
      const auto box = fui::makeRect(summary.x + i * (width + theme.spaceLg), summary.y, width, summary.height);
      small.maxLines = 1;
      screen.target().text(fui::makeRect(box.x, box.y, box.width, smallLine),
                           i ? tr(STR_FARKLE_SELECTED_POINTS) : tr(STR_FARKLE_AT_RISK), small);
      points(text, sizeof(text), i ? selectedPoints : state.turnPoints);
      auto amount = theme.titleText;
      amount.align = fui::TextAlign::Center;
      screen.target().text(fui::makeRect(box.x, box.y + smallLine, box.width, titleLine), text, amount);
    }
  }
}

void CasinoActivity::buildFarklePaytable(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto area = screen.contentRect();
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  const bool compact = area.height < smallLine * 17;
  if (compact) {
    static constexpr StrId LABELS[] = {
        StrId::STR_FARKLE_SCORE_ONE,         StrId::STR_FARKLE_SCORE_FIVE,         StrId::STR_FARKLE_SCORE_TRIPLE_ONE,
        StrId::STR_FARKLE_SCORE_TRIPLE_TWO,  StrId::STR_FARKLE_SCORE_TRIPLE_THREE, StrId::STR_FARKLE_SCORE_TRIPLE_FOUR,
        StrId::STR_FARKLE_SCORE_TRIPLE_FIVE, StrId::STR_FARKLE_SCORE_TRIPLE_SIX,   StrId::STR_FARKLE_SCORE_FOUR,
        StrId::STR_FARKLE_SCORE_FIVE_KIND,   StrId::STR_FARKLE_SCORE_SIX,          StrId::STR_FARKLE_SCORE_LOW,
        StrId::STR_FARKLE_SCORE_HIGH,        StrId::STR_FARKLE_SCORE_STRAIGHT};
    static constexpr uint16_t SCORES[] = {100, 50, 1000, 200, 300, 400, 500, 600, 2, 4, 8, 500, 750, 1500};
    const int16_t gap = theme.spaceLg;
    const int16_t width = (area.width - gap) / 2;
    const int16_t row = area.height / 7;
    auto label = theme.smallText;
    label.maxLines = 2;
    auto amount = theme.smallText;
    amount.bold = true;
    amount.align = fui::TextAlign::Right;
    for (int i = 0; i < 14; ++i) {
      const auto cell = fui::makeRect(area.x + (i / 7) * (width + gap), area.y + (i % 7) * row, width, row);
      char text[24];
      if (i >= 8 && i <= 10)
        snprintf(text, sizeof(text), tr(STR_FARKLE_TRIPLE_MULTIPLIER), static_cast<unsigned>(SCORES[i]));
      else
        points(text, sizeof(text), SCORES[i]);
      const int16_t scoreWidth = screen.target().measureText(amount.font, text, amount).width + theme.spaceSm;
      screen.target().text(fui::makeRect(cell.x, cell.y, cell.width - scoreWidth, cell.height), I18N.get(LABELS[i]),
                           label);
      screen.target().text(fui::makeRect(cell.right() - scoreWidth, cell.y, scoreWidth, cell.height), text, amount);
      screen.target().line(fui::Point{cell.x, cell.bottom()}, fui::Point{cell.right(), cell.bottom()}, 1, ink);
    }
    return;
  }
  // Each combination is drawn as the dice the player needs to recognize on the table.
  const int16_t gap = theme.spaceMd;
  const int16_t row = (area.height - smallLine * 4 - gap * 3 - theme.spaceSm * 4) / 8;
  auto label = theme.smallText;
  label.bold = true;
  auto amount = theme.bodyText;
  amount.bold = true;
  amount.align = fui::TextAlign::Right;
  auto section = [&](const char* text) {
    const auto band = screen.takeTop(smallLine, theme.spaceSm);
    screen.target().text(band, text, label);
  };
  auto combo = [&](fui::Rect cell, int first, int count, bool sequence, uint32_t score) {
    char text[16];
    points(text, sizeof(text), score);
    const int16_t scoreWidth = screen.target().measureText(amount.font, text, amount).width + theme.spaceSm;
    const int16_t side = std::min<int>(
        {row - theme.spaceSm, theme.minTouchSize, (cell.width - scoreWidth - theme.spaceSm * count) / count});
    for (int i = 0; i < count; ++i)
      ui_farkle::die(screen.target(),
                     fui::makeRect(cell.x + i * (side + theme.spaceSm), cell.y + (cell.height - side) / 2, side, side),
                     ink, first + (sequence ? i : 0));
    screen.target().text(fui::makeRect(cell.right() - scoreWidth, cell.y, scoreWidth, cell.height), text, amount);
  };
  section(tr(STR_FARKLE_SINGLES));
  auto band = screen.takeTop(row, gap);
  const int16_t half = (band.width - theme.spaceLg) / 2;
  combo(fui::makeRect(band.x, band.y, half, row), 1, 1, false, 100);
  combo(fui::makeRect(band.right() - half, band.y, half, row), 5, 1, false, 50);
  section(tr(STR_FARKLE_TRIPLES));
  for (int i = 0; i < 3; ++i) {
    band = screen.takeTop(row);
    for (int col = 0; col < 2; ++col) {
      const int face = i * 2 + col + 1;
      combo(fui::makeRect(band.x + col * (half + theme.spaceLg), band.y, half, row), face, 3, false,
            face == 1 ? 1000 : face * 100);
    }
  }
  screen.spacer(gap);
  section(tr(STR_FARKLE_MORE_KIND));
  band = screen.takeTop(row, gap);
  static constexpr StrId KINDS[] = {StrId::STR_FARKLE_FOUR_MULT, StrId::STR_FARKLE_FIVE_MULT,
                                    StrId::STR_FARKLE_SIX_MULT};
  auto mult = theme.smallText;
  mult.align = fui::TextAlign::Center;
  mult.maxLines = 2;
  for (int i = 0; i < 3; ++i)
    screen.target().text(fui::makeRect(band.x + i * band.width / 3, band.y, band.width / 3, band.height),
                         I18N.get(KINDS[i]), mult);
  section(tr(STR_FARKLE_STRAIGHTS));
  for (int i = 0; i < 3; ++i) {
    band = screen.takeTop(row);
    combo(band, i == 1 ? 2 : 1, i == 2 ? 6 : 5, true, i == 0 ? 500 : i == 1 ? 750 : 1500);
  }
}
