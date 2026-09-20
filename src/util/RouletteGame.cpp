#include "RouletteGame.h"

namespace {
using Bet = RouletteGame::Bet;
using Type = RouletteGame::Type;

Bet canonical(Bet bet) {
  if (bet.type == Type::Split && bet.first > bet.second) {
    const uint8_t first = bet.first;
    bet.first = bet.second;
    bet.second = first;
  }
  return bet;
}

bool sameBet(const Bet left, const Bet right) {
  return left.type == right.type && left.first == right.first && left.second == right.second;
}

bool validAmount(const int64_t amount) {
  return amount >= RouletteGame::MIN_BET_CENTS && amount <= RouletteGame::MAX_BET_CENTS && amount % 100 == 0;
}

int64_t calculateReturn(const RouletteGame::State& state) {
  int64_t returned = 0;
  for (size_t i = 0; i < state.betCount; ++i) {
    const auto& entry = state.bets[i];
    if (RouletteGame::covers(entry.bet, state.result))
      returned += entry.amountCents * (RouletteGame::profitOdds(entry.bet) + 1);
  }
  return returned;
}
}  // namespace

bool RouletteGame::validBet(const Bet bet) {
  if (bet.type == Type::Split) {
    if (bet.first >= bet.second || bet.second > 36) return false;
    if (bet.first == 0) return bet.second <= 3;
    return bet.second - bet.first == 3 || (bet.second - bet.first == 1 && (bet.first - 1) / 3 == (bet.second - 1) / 3);
  }
  if (bet.second != 0) return false;
  switch (bet.type) {
    case Type::Straight:
      return bet.first <= 36;
    case Type::Street:
      return bet.first >= 1 && bet.first <= 34 && bet.first % 3 == 1;
    case Type::Corner:
      return bet.first >= 1 && bet.first <= 32 && bet.first % 3 != 0;
    case Type::SixLine:
      return bet.first >= 1 && bet.first <= 31 && bet.first % 3 == 1;
    case Type::Trio:
      return bet.first == 1 || bet.first == 2;
    case Type::Dozen:
    case Type::Column:
      return bet.first >= 1 && bet.first <= 3;
    case Type::FirstFour:
    case Type::Red:
    case Type::Black:
    case Type::Odd:
    case Type::Even:
    case Type::Low:
    case Type::High:
      return bet.first == 0;
    default:
      return false;
  }
}

bool RouletteGame::isRed(const uint8_t result) {
  static constexpr uint64_t RED = (1ULL << 1) | (1ULL << 3) | (1ULL << 5) | (1ULL << 7) | (1ULL << 9) | (1ULL << 12) |
                                  (1ULL << 14) | (1ULL << 16) | (1ULL << 18) | (1ULL << 19) | (1ULL << 21) |
                                  (1ULL << 23) | (1ULL << 25) | (1ULL << 27) | (1ULL << 30) | (1ULL << 32) |
                                  (1ULL << 34) | (1ULL << 36);
  return result <= 36 && (RED & (1ULL << result));
}

bool RouletteGame::covers(const Bet bet, const uint8_t result) {
  if (result > 36 || !validBet(bet)) return false;
  switch (bet.type) {
    case Type::Straight:
      return result == bet.first;
    case Type::Split:
      return result == bet.first || result == bet.second;
    case Type::Street:
      return result >= bet.first && result <= bet.first + 2;
    case Type::Corner:
      return result == bet.first || result == bet.first + 1 || result == bet.first + 3 || result == bet.first + 4;
    case Type::SixLine:
      return result >= bet.first && result <= bet.first + 5;
    case Type::Trio:
      return result == 0 || result == bet.first || result == bet.first + 1;
    case Type::FirstFour:
      return result <= 3;
    case Type::Red:
      return isRed(result);
    case Type::Black:
      return result != 0 && !isRed(result);
    case Type::Odd:
      return result % 2 == 1;
    case Type::Even:
      return result != 0 && result % 2 == 0;
    case Type::Low:
      return result >= 1 && result <= 18;
    case Type::High:
      return result >= 19;
    case Type::Dozen:
      return result != 0 && (result - 1) / 12 + 1 == bet.first;
    case Type::Column:
      return result != 0 && (result - 1) % 3 + 1 == bet.first;
  }
  return false;
}

uint8_t RouletteGame::profitOdds(const Bet bet) {
  if (!validBet(bet)) return 0;
  switch (bet.type) {
    case Type::Straight:
      return 35;
    case Type::Split:
      return 17;
    case Type::Street:
    case Type::Trio:
      return 11;
    case Type::Corner:
    case Type::FirstFour:
      return 8;
    case Type::SixLine:
      return 5;
    case Type::Dozen:
    case Type::Column:
      return 2;
    default:
      return 1;
  }
}

void RouletteGame::reset() {
  for (auto& entry : current.bets) entry = Entry{};
  current.wagerCents = 0;
  current.returnCents = 0;
  current.betCount = 0;
  current.result = 0;
  current.phase = Phase::Betting;
}

bool RouletteGame::canAddBet(Bet bet, const int64_t amountCents, const int64_t availableCents) const {
  bet = canonical(bet);
  if (current.phase != Phase::Betting || !validBet(bet) || !validAmount(amountCents) || availableCents < 0 ||
      availableCents > MAX_BET_CENTS || current.wagerCents > MAX_BET_CENTS - amountCents ||
      current.wagerCents + amountCents > availableCents)
    return false;
  if (current.betCount < MAX_BETS) return true;
  for (size_t i = 0; i < current.betCount; ++i)
    if (sameBet(current.bets[i].bet, bet)) return true;
  return false;
}

bool RouletteGame::addBet(Bet bet, const int64_t amountCents, const int64_t availableCents) {
  if (!canAddBet(bet, amountCents, availableCents)) return false;
  bet = canonical(bet);
  size_t index = 0;
  while (index < current.betCount && !sameBet(current.bets[index].bet, bet)) ++index;
  if (index == current.betCount) {
    current.bets[index].bet = bet;
    ++current.betCount;
  }
  current.bets[index].amountCents += amountCents;
  current.wagerCents += amountCents;
  return true;
}

bool RouletteGame::removeBet(const size_t index) {
  if (current.phase != Phase::Betting || index >= current.betCount) return false;
  current.wagerCents -= current.bets[index].amountCents;
  for (size_t i = index; i + 1 < current.betCount; ++i) current.bets[i] = current.bets[i + 1];
  current.bets[--current.betCount] = Entry{};
  return true;
}

bool RouletteGame::clearBets() {
  if (current.phase != Phase::Betting || current.betCount == 0) return false;
  reset();
  return true;
}

bool RouletteGame::canSpin(const int64_t availableCents) const {
  return current.phase == Phase::Betting && current.betCount != 0 && availableCents >= current.wagerCents &&
         availableCents <= MAX_BET_CENTS;
}

bool RouletteGame::startRound(const int64_t availableCents, const Random random, void* context) {
  if (!random || !canSpin(availableCents)) return false;
  constexpr uint32_t POCKETS = 37;
  constexpr uint32_t THRESHOLD = (uint32_t{0} - POCKETS) % POCKETS;
  uint32_t sample;
  do {
    sample = random(context);
  } while (sample < THRESHOLD);
  current.result = sample % POCKETS;
  current.returnCents = calculateReturn(current);
  current.phase = Phase::Spinning;
  return true;
}

bool RouletteGame::reveal() {
  if (current.phase != Phase::Spinning) return false;
  current.phase = Phase::Settled;
  return true;
}

bool RouletteGame::nextRound(const bool keepBets) {
  if (current.phase != Phase::Settled) return false;
  if (!keepBets) {
    reset();
  } else {
    current.returnCents = 0;
    current.result = 0;
    current.phase = Phase::Betting;
  }
  return true;
}

bool RouletteGame::validateState(const State& state) {
  if (state.betCount > MAX_BETS || state.result > 36 ||
      (state.phase != Phase::Betting && state.phase != Phase::Spinning && state.phase != Phase::Settled))
    return false;
  int64_t wager = 0;
  for (size_t i = 0; i < MAX_BETS; ++i) {
    const auto& entry = state.bets[i];
    if (i >= state.betCount) {
      if (entry.amountCents != 0 || !sameBet(entry.bet, Bet{})) return false;
      continue;
    }
    if (!validAmount(entry.amountCents) || !validBet(entry.bet) || wager > MAX_BET_CENTS - entry.amountCents)
      return false;
    wager += entry.amountCents;
    for (size_t previous = 0; previous < i; ++previous)
      if (sameBet(entry.bet, state.bets[previous].bet)) return false;
  }
  if (state.wagerCents != wager) return false;
  if (state.phase == Phase::Betting) return state.returnCents == 0 && state.result == 0;
  return state.betCount != 0 && state.returnCents == calculateReturn(state);
}

bool RouletteGame::restore(const State& state) {
  if (!validateState(state)) return false;
  current = state;
  return true;
}
