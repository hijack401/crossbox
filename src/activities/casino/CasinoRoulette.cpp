#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CasinoActivity.h"
#include "components/UiCasino.h"
#include "components/UiRoulette.h"

namespace fui = freeink::ui;
namespace {
using Type = RouletteGame::Type;
using Bet = RouletteGame::Bet;
using Phase = RouletteGame::Phase;
constexpr Type INSIDE_TYPES[] = {Type::Split, Type::Street, Type::Corner, Type::SixLine, Type::Trio, Type::FirstFour};
constexpr StrId TYPE_LABELS[] = {
    StrId::STR_ROULETTE_NUMBER,   StrId::STR_ROULETTE_SPLIT, StrId::STR_ROULETTE_STREET,     StrId::STR_ROULETTE_CORNER,
    StrId::STR_ROULETTE_SIX_LINE, StrId::STR_ROULETTE_TRIO,  StrId::STR_ROULETTE_FIRST_FOUR, StrId::STR_ROULETTE_RED,
    StrId::STR_ROULETTE_BLACK,    StrId::STR_ROULETTE_ODD,   StrId::STR_ROULETTE_EVEN,       StrId::STR_ROULETTE_LOW,
    StrId::STR_ROULETTE_HIGH,     StrId::STR_ROULETTE_DOZEN, StrId::STR_ROULETTE_COLUMN};

const char* typeLabel(Type type) { return I18N.get(TYPE_LABELS[static_cast<uint8_t>(type)]); }

Bet outsideBet(int index) {
  if (index < 6) return Bet{static_cast<Type>(static_cast<int>(Type::Red) + index), 0, 0};
  return Bet{index < 9 ? Type::Dozen : Type::Column, static_cast<uint8_t>(index % 3 + 1), 0};
}

int optionCount(Type type) {
  int count = 0;
  for (uint8_t first = 0; first <= 36; ++first) {
    if (type == Type::Split) {
      for (uint8_t second = first + 1; second <= 36; ++second)
        if (RouletteGame::validBet(Bet{type, first, second})) ++count;
    } else if (RouletteGame::validBet(Bet{type, first, 0}))
      ++count;
  }
  return count;
}

Bet optionAt(Type type, int index) {
  for (uint8_t first = 0; first <= 36; ++first) {
    if (type == Type::Split) {
      for (uint8_t second = first + 1; second <= 36; ++second) {
        const Bet bet{type, first, second};
        if (RouletteGame::validBet(bet) && index-- == 0) return bet;
      }
    } else {
      const Bet bet{type, first, 0};
      if (RouletteGame::validBet(bet) && index-- == 0) return bet;
    }
  }
  return Bet{Type::Straight, 255, 0};
}

void betLabel(char* out, size_t size, Bet bet) {
  switch (bet.type) {
    case Type::Straight:
      snprintf(out, size, tr(STR_ROULETTE_NUMBER_VALUE), bet.first);
      break;
    case Type::Split:
      snprintf(out, size, tr(STR_ROULETTE_TWO_NUMBERS), bet.first, bet.second);
      break;
    case Type::Street:
      snprintf(out, size, tr(STR_ROULETTE_RANGE), bet.first, bet.first + 2);
      break;
    case Type::Corner:
      snprintf(out, size, tr(STR_ROULETTE_FOUR_NUMBERS), bet.first, bet.first + 1, bet.first + 3, bet.first + 4);
      break;
    case Type::SixLine:
      snprintf(out, size, tr(STR_ROULETTE_RANGE), bet.first, bet.first + 5);
      break;
    case Type::Trio:
      snprintf(out, size, tr(STR_ROULETTE_THREE_NUMBERS), 0, bet.first, bet.first + 1);
      break;
    case Type::FirstFour:
      snprintf(out, size, tr(STR_ROULETTE_RANGE), 0, 3);
      break;
    case Type::Dozen:
      snprintf(out, size, tr(STR_ROULETTE_RANGE), (bet.first - 1) * 12 + 1, bet.first * 12);
      break;
    case Type::Column:
      snprintf(out, size, tr(STR_ROULETTE_COLUMN_VALUE), bet.first);
      break;
    default:
      snprintf(out, size, "%s", typeLabel(bet.type));
      break;
  }
}

int64_t remainingFunds(const CasinoStore& store) {
  return std::max<int64_t>(0, store.game().state().balanceCents - store.roulette().state().wagerCents) / 100 * 100;
}
}  // namespace

void CasinoActivity::chooseRouletteBet(Bet bet) {
  if (!RouletteGame::validBet(bet)) return;
  rouletteBet = bet;
  normalizeBet();
  view = View::RouletteStake;
  selectedControl = R_ACCEPT;
}

void CasinoActivity::activateRoulette(int control) {
  auto& game = store.roulette();
  const auto& state = game.state();
  if (view == View::Table) {
    if (state.phase == Phase::Spinning) {
      if (control == R_REVEAL && store.revealRoulette()) {
        selectedControl = R_NEW;
        saveChanges();
      }
    } else if (state.phase == Phase::Settled) {
      if ((control == R_NEW || control == R_REPEAT) && game.nextRound(control == R_REPEAT)) {
        rouletteListPage = 0;
        normalizeBet();
        selectedControl = control == R_REPEAT ? R_SPIN : R_ADD;
        saveChanges();
      }
    } else if (control == R_ADD) {
      roulettePage = 0;
      view = View::RoulettePicker;
      selectedControl = R_CATEGORY_BASE + rouletteCategory;
    } else if (control == R_SPIN && store.spinRoulette(&CasinoActivity::randomWord)) {
      selectedControl = R_REVEAL;
      saveChanges();
    } else if (control == R_CLEAR && game.clearBets()) {
      rouletteListPage = 0;
      normalizeBet();
      selectedControl = R_ADD;
      saveChanges();
    } else if (control >= R_REMOVE_BASE && control < R_REMOVE_BASE + static_cast<int>(state.betCount)) {
      if (game.removeBet(control - R_REMOVE_BASE)) {
        normalizeBet();
        saveChanges();
      }
    } else if (control == R_PREVIOUS) {
      rouletteListPage = std::max(0, rouletteListPage - 1);
    } else if (control == R_NEXT && (rouletteListPage + 1) * rouletteListPerPage < state.betCount) {
      ++rouletteListPage;
    }
    return;
  }
  if (state.phase != Phase::Betting) return;
  if (view == View::RouletteStake) {
    if (control >= PRESET_BASE && control < PRESET_BASE + 4 && canPlaceBet(PRESETS[control - PRESET_BASE]))
      betCents = PRESETS[control - PRESET_BASE];
    else if (control == CUSTOM) {
      betInput[0] = '\0';
      view = View::BetEntry;
    } else if (control == MAX_BET) {
      betCents = remainingFunds(store);
      normalizeBet();
    } else if (control == R_ACCEPT && game.addBet(rouletteBet, betCents, store.game().state().balanceCents)) {
      view = View::Table;
      rouletteListPage = 0;
      selectedControl = R_SPIN;
      saveChanges();
    }
    return;
  }
  if (control >= R_CATEGORY_BASE && control < R_CATEGORY_BASE + 3) {
    rouletteCategory = control - R_CATEGORY_BASE;
    roulettePage = 0;
    view = View::RoulettePicker;
  } else if (control == R_PREVIOUS) {
    roulettePage = std::max(0, roulettePage - 1);
  } else if (control == R_NEXT && roulettePage + 1 < roulettePages) {
    ++roulettePage;
  } else if (view == View::RoulettePicker && rouletteCategory == 2 && control >= R_TYPE_BASE &&
             control < R_TYPE_BASE + 6) {
    rouletteBet = Bet{INSIDE_TYPES[control - R_TYPE_BASE], 0, 0};
    roulettePage = 0;
    if (rouletteBet.type == Type::FirstFour)
      chooseRouletteBet(rouletteBet);
    else
      view = View::RouletteOptions;
  } else if (control >= R_CHOICE_BASE && control < R_CHOICE_BASE + 13) {
    const int choice = control - R_CHOICE_BASE;
    const int index = roulettePage * rouletteChoicesPerPage + choice;
    if (view == View::RouletteOptions) {
      chooseRouletteBet(optionAt(rouletteBet.type, index));
    } else if (rouletteCategory == 0 && index < 12) {
      chooseRouletteBet(outsideBet(index));
    } else if (rouletteCategory == 1) {
      const int number = choice == 0 ? 0 : roulettePage * rouletteChoicesPerPage + choice;
      if (number <= 36) chooseRouletteBet(Bet{Type::Straight, static_cast<uint8_t>(number), 0});
    }
  }
}

void CasinoActivity::drawRoulettePaging(UiScreen& screen, fui::Rect area, int page, int pages) {
  const auto& theme = screen.theme();
  const int16_t width = (area.width - theme.spaceSm * 2) / 3;
  drawButton(screen, fui::Rect{area.x, area.y, width, area.height}, tr(STR_TODO_PREVIOUS), R_PREVIOUS, false, page > 0);
  char text[24];
  snprintf(text, sizeof(text), tr(STR_ROULETTE_PAGE), page + 1, pages);
  drawLabel(screen, fui::Rect{static_cast<int16_t>(area.x + width + theme.spaceSm), area.y, width, area.height}, text,
            false, fui::TextAlign::Center);
  drawButton(screen, fui::Rect{static_cast<int16_t>(area.right() - width), area.y, width, area.height},
             tr(STR_TODO_NEXT), R_NEXT, false, page + 1 < pages);
}

void CasinoActivity::buildRouletteBetting(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& state = store.roulette().state();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  const int16_t row = theme.rowHeight;
  const auto spin = screen.takeBottom(row, theme.spaceMd);
  const auto actions = screen.takeBottom(row, theme.spaceMd);
  const int16_t half = (actions.width - theme.spaceMd) / 2;
  drawButton(screen, fui::Rect{actions.x, actions.y, half, actions.height}, tr(STR_ROULETTE_ADD), R_ADD, false,
             remainingFunds(store) >= 100);
  drawButton(screen, fui::Rect{static_cast<int16_t>(actions.right() - half), actions.y, half, actions.height},
             tr(STR_ROULETTE_CLEAR), R_CLEAR, false, state.betCount != 0);
  drawButton(screen, spin, tr(STR_ROULETTE_SPIN), R_SPIN, store.roulette().canSpin(store.game().state().balanceCents),
             store.roulette().canSpin(store.game().state().balanceCents));
  ui_roulette::caption(screen.target(), screen.takeTop(smallLine, theme.spaceMd), theme,
                       tr(STR_ROULETTE_TABLE_EDITION));
  if (!state.betCount) {
    drawLabel(screen, screen.takeBottom(line * 2, theme.spaceMd),
              remainingFunds(store) >= 100 ? tr(STR_ROULETTE_PLACE_HINT) : tr(STR_CASINO_NO_FUNDS), false,
              fui::TextAlign::Center);
    ui_roulette::wheel(screen.target(), screen.contentRect(), theme);
    return;
  }
  char amount[48];
  char label[80];
  const bool compact = screen.contentRect().height < row * 5;
  const auto summary = screen.takeTop(compact ? line * 2 + theme.spaceMd : row * 2 + theme.spaceMd, theme.spaceMd);
  const int16_t wheelSide = compact ? 0 : std::min<int16_t>(summary.height, summary.width / 3);
  if (wheelSide) ui_roulette::wheel(screen.target(), fui::Rect{summary.x, summary.y, wheelSide, summary.height}, theme);
  const int16_t textX = summary.x + wheelSide + (wheelSide ? theme.spaceMd : 0);
  const int16_t textWidth = summary.right() - textX;
  screen.target().text(fui::Rect{textX, summary.y, textWidth, smallLine}, tr(STR_ROULETTE_BETTING_SLIP),
                       theme.smallText);
  ui_casino::balance(screen.target(),
                     fui::Rect{textX, static_cast<int16_t>(summary.y + smallLine + theme.spaceSm), textWidth,
                               static_cast<int16_t>(summary.height - smallLine - theme.spaceSm)},
                     theme, state.wagerCents);
  if (state.wagerCents > store.game().state().balanceCents)
    drawLabel(screen, screen.takeTop(line * 2, theme.spaceSm), tr(STR_ROULETTE_REDUCE));
  const auto pagination = screen.takeBottom(theme.minTouchSize, theme.spaceSm);
  const auto area = screen.contentRect();
  rouletteListPerPage = std::clamp<int>(area.height / (row + theme.spaceSm), 1, 5);
  const int pages = (state.betCount + rouletteListPerPage - 1) / rouletteListPerPage;
  rouletteListPage = std::clamp(rouletteListPage, 0, pages - 1);
  if (pages > 1)
    drawRoulettePaging(screen, pagination, rouletteListPage, pages);
  else
    drawLabel(screen, pagination, tr(STR_ROULETTE_REMOVE_HINT), false, fui::TextAlign::Center);
  for (int i = rouletteListPage * rouletteListPerPage;
       i < std::min<int>(state.betCount, (rouletteListPage + 1) * rouletteListPerPage); ++i) {
    const auto& entry = state.bets[i];
    const auto rect = screen.takeTop(row, theme.spaceSm);
    drawButton(screen, rect, nullptr, R_REMOVE_BASE + i);
    const auto resolved = screen.frame().stateFor(ACTION_CONTROL, R_REMOVE_BASE + i, buttonProps.state);
    const auto foreground = buttonProps.styles.resolve(resolved).foreground;
    auto text = fui::textStyleWithForeground(theme.bodyText, foreground);
    const int16_t inset = theme.spaceMd;
    const int16_t cross = theme.minTouchSize;
    const int16_t token = std::min<int16_t>(line, rect.width / 12);
    ui_roulette::chip(screen.target(), fui::Rect{static_cast<int16_t>(rect.x + inset), rect.y, token, rect.height},
                      foreground);
    const int16_t labelX = rect.x + inset * 2 + token;
    const int16_t labelWidth = rect.width * 3 / 5 - inset * 2 - token;
    betLabel(label, sizeof(label), entry.bet);
    text.bold = true;
    screen.target().text(fui::Rect{labelX, rect.y, labelWidth, rect.height}, label, text);
    ui_casino::money(amount, sizeof(amount), entry.amountCents);
    text.bold = false;
    text.align = fui::TextAlign::Right;
    screen.target().text(fui::Rect{static_cast<int16_t>(rect.x + rect.width * 3 / 5), rect.y,
                                   static_cast<int16_t>(rect.width * 2 / 5 - cross), rect.height},
                         amount, text);
    text.align = fui::TextAlign::Center;
    screen.target().text(fui::Rect{static_cast<int16_t>(rect.right() - cross), rect.y, cross, rect.height},
                         tr(STR_ROULETTE_REMOVE), text);
  }
}

void CasinoActivity::buildRoulettePicker(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const bool options = view == View::RouletteOptions;
  drawLabel(screen, screen.takeTop(line, theme.spaceLg), options ? typeLabel(rouletteBet.type) : tr(STR_ROULETTE_PLACE),
            true);
  const auto tabs = screen.takeTop(theme.minTouchSize, theme.spaceLg);
  static constexpr StrId CATEGORIES[] = {StrId::STR_ROULETTE_OUTSIDE, StrId::STR_ROULETTE_NUMBERS,
                                         StrId::STR_ROULETTE_INSIDE};
  const int16_t tabWidth = (tabs.width - theme.spaceSm * 2) / 3;
  for (int i = 0; i < 3; ++i)
    drawButton(screen,
               fui::Rect{static_cast<int16_t>(tabs.x + i * (tabWidth + theme.spaceSm)), tabs.y, tabWidth, tabs.height},
               I18N.get(CATEGORIES[i]), R_CATEGORY_BASE + i, rouletteCategory == i);
  const auto footer = screen.takeBottom(theme.minTouchSize, theme.spaceMd);
  if (!options && rouletteCategory == 2) {
    const auto area = screen.contentRect();
    const int columns = area.height < theme.minTouchSize * 3 + theme.spaceMd * 2 ? 3 : 2;
    const int rows = 6 / columns;
    const int16_t width = (area.width - theme.spaceMd * (columns - 1)) / columns;
    const int16_t row = std::min<int16_t>(theme.rowHeight * 2, (area.height - theme.spaceMd * (rows - 1)) / rows);
    for (int i = 0; i < 6; ++i) {
      drawButton(screen,
                 fui::Rect{static_cast<int16_t>(area.x + (i % columns) * (width + theme.spaceMd)),
                           static_cast<int16_t>(area.y + (i / columns) * (row + theme.spaceMd)), width, row},
                 typeLabel(INSIDE_TYPES[i]), R_TYPE_BASE + i);
    }
    drawButton(screen, footer, tr(STR_DONE), CLOSE);
    return;
  }
  if (!options && rouletteCategory == 0 && screen.contentRect().height >= theme.minTouchSize * 5 + theme.spaceSm * 4) {
    const auto area = screen.contentRect();
    const int16_t height = (area.height - theme.spaceSm * 4) / 5;
    rouletteChoicesPerPage = 12;
    roulettePage = 0;
    roulettePages = 1;
    for (int i = 0; i < 12; ++i) {
      const int columns = i < 6 ? 2 : 3;
      const int row = i < 6 ? i / 2 : 3 + (i - 6) / 3;
      const int column = i < 6 ? i % 2 : (i - 6) % 3;
      const int16_t width = (area.width - (columns - 1) * theme.spaceSm) / columns;
      char label[32];
      betLabel(label, sizeof(label), outsideBet(i));
      drawButton(screen,
                 fui::Rect{static_cast<int16_t>(area.x + column * (width + theme.spaceSm)),
                           static_cast<int16_t>(area.y + row * (height + theme.spaceSm)), width, height},
                 label, R_CHOICE_BASE + i);
    }
    drawButton(screen, footer, tr(STR_DONE), CLOSE);
    return;
  }
  const bool numbers = !options && rouletteCategory == 1;
  if (numbers)
    drawButton(screen, screen.takeTop(theme.minTouchSize, theme.spaceSm), tr(STR_ROULETTE_ZERO_NUMBER), R_CHOICE_BASE);
  const int columns = numbers ? 3 : options && rouletteBet.type != Type::Corner ? 3 : 2;
  const auto area = screen.contentRect();
  const int rows = std::clamp<int>((area.height + theme.spaceSm) / (theme.rowHeight + theme.spaceSm), 1, 12 / columns);
  rouletteChoicesPerPage = rows * columns;
  const int count = numbers ? 36 : options ? optionCount(rouletteBet.type) : 12;
  roulettePages = (count + rouletteChoicesPerPage - 1) / rouletteChoicesPerPage;
  roulettePage = std::clamp(roulettePage, 0, roulettePages - 1);
  const int16_t width = (area.width - (columns - 1) * theme.spaceSm) / columns;
  const int16_t height = std::min<int16_t>(theme.rowHeight * 2, (area.height - (rows - 1) * theme.spaceSm) / rows);
  for (int i = 0; i < rouletteChoicesPerPage && roulettePage * rouletteChoicesPerPage + i < count; ++i) {
    const int index = roulettePage * rouletteChoicesPerPage + i;
    const Bet bet = numbers   ? Bet{Type::Straight, static_cast<uint8_t>(index + 1), 0}
                    : options ? optionAt(rouletteBet.type, index)
                              : outsideBet(index);
    char label[48];
    if (numbers)
      snprintf(label, sizeof(label), tr(STR_CASINO_TOTAL), bet.first);
    else
      betLabel(label, sizeof(label), bet);
    const fui::Rect pocket{static_cast<int16_t>(area.x + (i % columns) * (width + theme.spaceSm)),
                           static_cast<int16_t>(area.y + (i / columns) * (height + theme.spaceSm)), width, height};
    drawButton(screen, pocket, label, R_CHOICE_BASE + i + (numbers ? 1 : 0));
    if (numbers) {
      const auto ink = fui::Paint::solid(theme.bodyText.color);
      const auto red =
          fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
      screen.target().fill(
          fui::Rect{static_cast<int16_t>(pocket.x + theme.spaceSm), static_cast<int16_t>(pocket.y + theme.spaceSm),
                    static_cast<int16_t>(std::max<int16_t>(2, theme.spaceSm)),
                    static_cast<int16_t>(pocket.height - theme.spaceSm * 2)},
          RouletteGame::isRed(bet.first) ? red : ink);
    }
  }
  if (roulettePages > 1)
    drawRoulettePaging(screen, footer, roulettePage, roulettePages);
  else
    drawButton(screen, footer, tr(STR_DONE), CLOSE);
}

void CasinoActivity::buildRouletteStake(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
  const auto add = screen.takeBottom(theme.rowHeight, theme.spaceMd);
  const auto custom = screen.takeBottom(theme.rowHeight, theme.spaceMd);
  const auto presets = screen.takeBottom(theme.rowHeight, theme.spaceLg);
  char label[80];
  char amount[48];
  ui_roulette::caption(screen.target(), screen.takeTop(line, theme.spaceMd), theme, tr(STR_ROULETTE_BETTING_SLIP));
  const auto position = screen.takeTop(titleLine + line + theme.spaceLg * 2, theme.spaceLg);
  ui_roulette::frame(screen.target(), position, theme);
  betLabel(label, sizeof(label), rouletteBet);
  drawLabel(screen, fui::Rect{position.x, static_cast<int16_t>(position.y + theme.spaceMd), position.width, titleLine},
            label, true, fui::TextAlign::Center);
  snprintf(label, sizeof(label), tr(STR_ROULETTE_PAYS), RouletteGame::profitOdds(rouletteBet));
  drawLabel(screen,
            fui::Rect{position.x, static_cast<int16_t>(position.bottom() - line - theme.spaceMd), position.width, line},
            label, false, fui::TextAlign::Center);
  ui_casino::money(amount, sizeof(amount), remainingFunds(store));
  snprintf(label, sizeof(label), tr(STR_ROULETTE_AVAILABLE), amount);
  drawLabel(screen, screen.takeBottom(line, theme.spaceMd), label, false, fui::TextAlign::Center);
  if (store.roulette().state().betCount == RouletteGame::MAX_BETS && !canPlaceBet(100))
    drawLabel(screen, screen.contentRect(), tr(STR_ROULETTE_LIMIT), false, fui::TextAlign::Center);
  else {
    const auto area = screen.contentRect();
    const int16_t height = std::min<int16_t>(area.height, line + balance_font::HEIGHT + theme.spaceMd * 2);
    const int16_t top = area.y + (area.height - height) / 2;
    drawLabel(screen, fui::Rect{area.x, top, area.width, line}, tr(STR_CASINO_BET));
    const int16_t token = std::min<int16_t>(theme.rowHeight, area.width / 5);
    const int16_t moneyTop = top + line + theme.spaceMd;
    const int16_t moneyHeight = std::max<int16_t>(0, height - line - theme.spaceMd);
    ui_casino::balance(
        screen.target(),
        fui::Rect{area.x, moneyTop, static_cast<int16_t>(area.width - token - theme.spaceLg), moneyHeight}, theme,
        betCents);
    ui_roulette::chip(screen.target(),
                      fui::Rect{static_cast<int16_t>(area.right() - token), moneyTop, token, moneyHeight},
                      fui::Paint::solid(theme.bodyText.color));
  }
  const int16_t width = (presets.width - theme.spaceSm * 3) / 4;
  for (int i = 0; i < 4; ++i) {
    ui_casino::money(amount, sizeof(amount), PRESETS[i]);
    drawButton(
        screen,
        fui::Rect{static_cast<int16_t>(presets.x + i * (width + theme.spaceSm)), presets.y, width, presets.height},
        amount, PRESET_BASE + i, betCents == PRESETS[i], canPlaceBet(PRESETS[i]));
  }
  const int16_t half = (custom.width - theme.spaceMd) / 2;
  drawButton(screen, fui::Rect{custom.x, custom.y, half, custom.height}, tr(STR_CASINO_CUSTOM), CUSTOM, false,
             canPlaceBet(100));
  drawButton(screen, fui::Rect{static_cast<int16_t>(custom.right() - half), custom.y, half, custom.height},
             tr(STR_CASINO_MAX), MAX_BET, false, canPlaceBet(remainingFunds(store)));
  drawButton(screen, add, tr(STR_ROULETTE_ADD), R_ACCEPT, canPlaceBet(betCents), canPlaceBet(betCents));
}

void CasinoActivity::buildRouletteRound(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& state = store.roulette().state();
  const bool complete = state.phase == Phase::Settled;
  const int16_t line = screen.target().lineHeight(theme.bodyText.font);
  const int16_t titleLine = screen.target().lineHeight(theme.titleText.font);
  const int16_t smallLine = screen.target().lineHeight(theme.smallText.font);
  const auto actions = screen.takeBottom(theme.rowHeight, theme.spaceMd);
  if (complete) {
    const int16_t half = (actions.width - theme.spaceMd) / 2;
    drawButton(screen, fui::Rect{actions.x, actions.y, half, actions.height}, tr(STR_ROULETTE_NEW), R_NEW);
    drawButton(screen, fui::Rect{static_cast<int16_t>(actions.right() - half), actions.y, half, actions.height},
               tr(STR_ROULETTE_REPEAT), R_REPEAT, state.wagerCents <= store.game().state().balanceCents,
               state.wagerCents <= store.game().state().balanceCents);
  } else
    drawButton(screen, actions, tr(STR_ROULETTE_REVEAL), R_REVEAL, true);
  const auto result = screen.takeBottom(titleLine + line + smallLine + theme.spaceMd * 4, theme.spaceMd);
  ui_roulette::frame(screen.target(), result, theme);
  ui_roulette::caption(screen.target(), screen.takeTop(smallLine, theme.spaceSm), theme,
                       complete ? tr(STR_ROULETTE_WINNING_POCKET) : tr(STR_ROULETTE_BALL_IN_PLAY));
  char amount[48];
  char label[80];
  ui_casino::money(amount, sizeof(amount), state.wagerCents);
  snprintf(label, sizeof(label), tr(STR_ROULETTE_TOTAL), amount);
  drawLabel(screen, screen.takeBottom(line, theme.spaceSm), label, false, fui::TextAlign::Center);
  ui_roulette::wheel(screen.target(), screen.contentRect(), theme, complete, state.result, !complete);
  if (complete) {
    const int64_t net = state.returnCents - state.wagerCents;
    drawLabel(screen,
              fui::Rect{static_cast<int16_t>(result.x + theme.spaceMd), static_cast<int16_t>(result.y + theme.spaceMd),
                        static_cast<int16_t>(result.width - theme.spaceMd * 2), titleLine},
              net > 0    ? tr(STR_CASINO_WIN)
              : net == 0 ? tr(STR_ROULETTE_EVEN_RESULT)
                         : tr(STR_BACCARAT_LOSE),
              true, fui::TextAlign::Center);
    ui_casino::money(amount, sizeof(amount), net < 0 ? -net : net);
    snprintf(label, sizeof(label), net < 0 ? tr(STR_CASINO_MINUS) : tr(STR_CASINO_PLUS), amount);
    auto text = theme.bodyText;
    text.bold = true;
    text.align = fui::TextAlign::Center;
    screen.target().text(fui::Rect{result.x, static_cast<int16_t>(result.y + theme.spaceMd + titleLine + theme.spaceSm),
                                   result.width, line},
                         label, text);
    ui_casino::money(amount, sizeof(amount), state.returnCents);
    snprintf(label, sizeof(label), tr(STR_ROULETTE_RETURN), amount);
    text = theme.smallText;
    text.align = fui::TextAlign::Center;
    screen.target().text(
        fui::Rect{result.x, static_cast<int16_t>(result.bottom() - smallLine - theme.spaceMd), result.width, smallLine},
        label, text);
  } else {
    drawLabel(screen, fui::Rect{result.x, static_cast<int16_t>(result.y + theme.spaceMd), result.width, titleLine},
              tr(STR_ROULETTE_CLOSED), true, fui::TextAlign::Center);
    drawLabel(screen,
              fui::Rect{static_cast<int16_t>(result.x + theme.spaceMd * 2),
                        static_cast<int16_t>(result.y + theme.spaceMd + titleLine),
                        static_cast<int16_t>(result.width - theme.spaceMd * 4), static_cast<int16_t>(line * 2)},
              tr(STR_ROULETTE_LOCKED_HINT), false, fui::TextAlign::Center);
  }
}
