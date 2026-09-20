#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CasinoActivity.h"
#include "components/UiCasino.h"
#include "components/UiSlots.h"

namespace fui = freeink::ui;
namespace {
using Phase = SlotsGame::Phase;
using Symbol = SlotsGame::Symbol;
}  // namespace

void CasinoActivity::activateSlots(int control) {
  auto& game = store.slots();
  const auto& state = game.state();
  if (state.phase == Phase::Revealing) {
    if (control == S_REVEAL && store.revealSlots()) {
      selectedControl = state.phase == Phase::Settled ? S_AGAIN : S_REVEAL;
      saveChanges();
    }
    return;
  }
  if (state.phase == Phase::Settled) {
    if (control == S_AGAIN && store.spinSlots(state.wagerCents, &CasinoActivity::randomWord)) {
      selectedControl = S_REVEAL;
      saveChanges();
    } else if (control == S_CHANGE_BET) {
      betCents = state.wagerCents;
      if (game.nextRound()) {
        normalizeBet();
        selectedControl = S_SPIN;
        saveChanges();
      }
    }
    return;
  }
  if (control >= PRESET_BASE && control < PRESET_BASE + 4 && canPlaceBet(PRESETS[control - PRESET_BASE])) {
    betCents = PRESETS[control - PRESET_BASE];
  } else if (control == CUSTOM) {
    betInput[0] = '\0';
    view = View::BetEntry;
  } else if (control == MAX_BET) {
    betCents = store.game().state().balanceCents / 100 * 100;
    normalizeBet();
  } else if (control == S_SPIN && store.spinSlots(betCents, &CasinoActivity::randomWord)) {
    selectedControl = S_REVEAL;
    saveChanges();
  }
}

void CasinoActivity::buildSlotsBetting(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const bool compact = screen.contentRect().height < theme.rowHeight * 7;
  const int16_t row = compact ? theme.minTouchSize : theme.rowHeight;
  const int16_t gap = compact ? theme.spaceSm : theme.spaceMd;
  const auto spin = screen.takeBottom(row, gap);
  const auto custom = screen.takeBottom(row, gap);
  const auto presets = screen.takeBottom(row, gap);
  const bool funded = store.game().state().balanceCents >= SlotsGame::MIN_BET_CENTS;
  const bool canSpin = canPlaceBet(betCents);
  drawButton(screen, spin, tr(STR_ROULETTE_SPIN), S_SPIN, canSpin, canSpin);
  const int16_t half = (custom.width - theme.spaceMd) / 2;
  drawButton(screen, fui::Rect{custom.x, custom.y, half, custom.height}, tr(STR_CASINO_CUSTOM), CUSTOM, false, funded);
  drawButton(screen, fui::Rect{static_cast<int16_t>(custom.right() - half), custom.y, half, custom.height},
             tr(STR_CASINO_MAX), MAX_BET, false, funded);
  const int16_t cell = (presets.width - theme.spaceSm * 3) / 4;
  for (int i = 0; i < 4; ++i) {
    char amount[24];
    ui_casino::money(amount, sizeof(amount), PRESETS[i]);
    const bool enabled = canPlaceBet(PRESETS[i]);
    drawButton(screen,
               fui::Rect{static_cast<int16_t>(presets.x + i * (cell + theme.spaceSm)), presets.y, cell, presets.height},
               amount, PRESET_BASE + i, enabled && betCents == PRESETS[i], enabled);
  }
  const auto area = screen.contentRect();
  const int16_t reelHeight = std::min<int16_t>(theme.rowHeight * 3, area.height / 2);
  const int16_t amountHeight = std::min<int16_t>(theme.rowHeight * 2, area.height - reelHeight - line - gap * 2);
  const int16_t groupHeight = reelHeight + line + amountHeight + gap * 2;
  screen.spacer(std::max<int16_t>(0, (area.height - groupHeight) / 2));
  ui_slots::reels(screen.target(), screen.takeTop(reelHeight, gap), theme, store.slots().state(), true);
  if (funded) {
    drawLabel(screen, screen.takeTop(line, gap), tr(STR_CASINO_BET), false, fui::TextAlign::Center);
    ui_casino::amount(screen.target(), screen.takeTop(amountHeight), theme, betCents);
  } else {
    drawLabel(screen, screen.contentRect(), tr(STR_CASINO_NO_FUNDS), false, fui::TextAlign::Center);
  }
}

void CasinoActivity::buildSlotsRound(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& state = store.slots().state();
  const bool settled = state.phase == Phase::Settled;
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const bool compact = screen.contentRect().height < theme.rowHeight * 7;
  const int16_t row = compact ? theme.minTouchSize : theme.rowHeight;
  const auto action = screen.takeBottom(row, theme.spaceMd);
  if (settled) {
    const int16_t half = (action.width - theme.spaceMd) / 2;
    const bool canRepeat = canPlaceBet(state.wagerCents);
    drawButton(screen, fui::Rect{action.x, action.y, half, action.height}, tr(STR_SLOTS_CHANGE_BET), S_CHANGE_BET);
    drawButton(screen, fui::Rect{static_cast<int16_t>(action.right() - half), action.y, half, action.height},
               tr(STR_SLOTS_SPIN_AGAIN), S_AGAIN, canRepeat, canRepeat);
  } else {
    drawButton(screen, action, tr(STR_SLOTS_REVEAL), S_REVEAL, true);
  }
  char money[48];
  char label[80];
  ui_casino::money(money, sizeof(money), state.wagerCents);
  snprintf(label, sizeof(label), tr(STR_CASINO_BET_AMOUNT), money);
  drawLabel(screen, screen.takeTop(line, theme.spaceLg), label, false, fui::TextAlign::Center);
  const auto area = screen.contentRect();
  const int16_t resultHeight = line * 3 + theme.spaceMd * 2;
  const int16_t reelHeight = std::min<int16_t>(theme.rowHeight * 3, area.height - resultHeight - theme.spaceLg);
  screen.spacer(std::max<int16_t>(0, (area.height - reelHeight - resultHeight - theme.spaceLg) / 2));
  ui_slots::reels(screen.target(), screen.takeTop(reelHeight, theme.spaceLg), theme, state);
  if (!settled) return;
  const uint8_t multiplier = SlotsGame::returnMultiplier(state.reels);
  drawLabel(screen, screen.takeTop(line, theme.spaceMd),
            multiplier == SlotsGame::MAX_RETURN_MULTIPLIER ? tr(STR_SLOTS_JACKPOT)
            : multiplier                                   ? tr(STR_CASINO_WIN)
                                                           : tr(STR_SLOTS_NO_WIN),
            true, fui::TextAlign::Center);
  const int64_t net = state.returnCents - state.wagerCents;
  ui_casino::money(money, sizeof(money), net < 0 ? -net : net);
  snprintf(label, sizeof(label), net >= 0 ? tr(STR_CASINO_PLUS) : tr(STR_CASINO_MINUS), money);
  drawLabel(screen, screen.takeTop(line, theme.spaceMd), label, true, fui::TextAlign::Center);
  if (state.returnCents > 0) {
    ui_casino::money(money, sizeof(money), state.returnCents);
    snprintf(label, sizeof(label), tr(STR_ROULETTE_RETURN), money);
    drawLabel(screen, screen.takeTop(line), label, false, fui::TextAlign::Center);
  }
}

void CasinoActivity::buildSlotsPaytable(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.smallText.font);
  auto caption = theme.smallText;
  caption.maxLines = 2;
  screen.target().text(screen.takeTop(line * 2, theme.spaceSm), tr(STR_SLOTS_TOTAL_RETURN), caption);
  static constexpr Symbol SYMBOLS[] = {Symbol::Seven, Symbol::Bar, Symbol::Bell, Symbol::Lemon, Symbol::Cherry};
  const int16_t rowHeight = screen.contentRect().height / 6;
  for (int i = 0; i < 6; ++i) {
    const auto row = screen.takeTop(rowHeight);
    const int16_t labelWidth = row.width / 4;
    const int16_t iconsWidth = row.width - labelWidth - theme.spaceMd;
    const auto type = i < 5 ? SYMBOLS[i] : Symbol::Cherry;
    const Symbol symbols[] = {type, type, i < 5 ? type : Symbol::Lemon};
    char multiplier[16];
    snprintf(multiplier, sizeof(multiplier), tr(STR_SLOTS_MULTIPLIER),
             static_cast<unsigned>(SlotsGame::returnMultiplier(symbols)));
    if (i < 5) {
      const int16_t side = std::min<int16_t>(row.height - theme.spaceMd, iconsWidth / 3);
      for (int reel = 0; reel < 3; ++reel)
        ui_slots::symbol(screen.target(),
                         fui::Rect{static_cast<int16_t>(row.x + reel * iconsWidth / 3 + (iconsWidth / 3 - side) / 2),
                                   static_cast<int16_t>(row.y + (row.height - side) / 2), side, side},
                         theme, type);
    } else {
      drawLabel(screen, fui::Rect{row.x, row.y, iconsWidth, row.height}, tr(STR_SLOTS_TWO_CHERRIES));
    }
    drawLabel(screen, fui::Rect{static_cast<int16_t>(row.right() - labelWidth), row.y, labelWidth, row.height},
              multiplier, true, fui::TextAlign::Right);
    if (i < 5)
      screen.target().fill(
          fui::Rect{row.x, row.bottom(), row.width, 1},
          fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray));
  }
}
