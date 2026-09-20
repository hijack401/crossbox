#include "SlotsGame.h"

namespace {
bool validSymbol(const SlotsGame::Symbol symbol) { return symbol <= SlotsGame::Symbol::Seven; }

bool validWager(const int64_t amount) {
  return amount >= SlotsGame::MIN_BET_CENTS && amount <= SlotsGame::MAX_BET_CENTS && amount % 100 == 0;
}
}  // namespace

SlotsGame::Symbol SlotsGame::symbolForStop(const uint8_t stop) {
  if (stop < 6) return Symbol::Cherry;
  if (stop < 11) return Symbol::Lemon;
  if (stop < 15) return Symbol::Bell;
  if (stop < 18) return Symbol::Bar;
  if (stop < 20) return Symbol::Seven;
  return static_cast<Symbol>(255);
}

uint8_t SlotsGame::returnMultiplier(const Symbol reels[REELS]) {
  if (!reels) return 0;
  uint8_t cherries = 0;
  for (size_t i = 0; i < REELS; ++i) {
    if (!validSymbol(reels[i])) return 0;
    cherries += reels[i] == Symbol::Cherry;
  }
  if (reels[0] == reels[1] && reels[1] == reels[2]) {
    static constexpr uint8_t RETURNS[] = {5, 8, 15, 30, MAX_RETURN_MULTIPLIER};
    return RETURNS[static_cast<uint8_t>(reels[0])];
  }
  return cherries == 2 ? 2 : 0;
}

void SlotsGame::reset() {
  current.wagerCents = 0;
  current.returnCents = 0;
  for (auto& reel : current.reels) reel = Symbol::Cherry;
  current.revealedReels = 0;
  current.phase = Phase::Betting;
}

bool SlotsGame::canBet(const int64_t wagerCents, const int64_t availableCents) const {
  return (current.phase == Phase::Betting || current.phase == Phase::Settled) && validWager(wagerCents) &&
         availableCents >= wagerCents && availableCents <= MAX_BET_CENTS;
}

bool SlotsGame::startRound(const int64_t wagerCents, const int64_t availableCents, const Random random, void* context) {
  if (!random || !canBet(wagerCents, availableCents)) return false;
  constexpr uint32_t STOPS = 20;
  constexpr uint32_t THRESHOLD = (uint32_t{0} - STOPS) % STOPS;
  for (auto& reel : current.reels) {
    uint32_t sample;
    do {
      sample = random(context);
    } while (sample < THRESHOLD);
    reel = symbolForStop(sample % STOPS);
  }
  current.wagerCents = wagerCents;
  current.returnCents = wagerCents * returnMultiplier(current.reels);
  current.revealedReels = 0;
  current.phase = Phase::Revealing;
  return true;
}

bool SlotsGame::revealNext() {
  if (current.phase != Phase::Revealing) return false;
  if (++current.revealedReels == REELS) current.phase = Phase::Settled;
  return true;
}

bool SlotsGame::nextRound() {
  if (current.phase != Phase::Settled) return false;
  reset();
  return true;
}

bool SlotsGame::reelRevealed(const size_t index) const { return index < REELS && index < current.revealedReels; }

bool SlotsGame::validateState(const State& state) {
  if (state.phase != Phase::Betting && state.phase != Phase::Revealing && state.phase != Phase::Settled) return false;
  for (const auto reel : state.reels)
    if (!validSymbol(reel)) return false;
  if (state.phase == Phase::Betting) {
    if (state.wagerCents != 0 || state.returnCents != 0 || state.revealedReels != 0) return false;
    for (const auto reel : state.reels)
      if (reel != Symbol::Cherry) return false;
    return true;
  }
  if (!validWager(state.wagerCents) || state.returnCents != state.wagerCents * returnMultiplier(state.reels))
    return false;
  return state.phase == Phase::Revealing ? state.revealedReels < REELS : state.revealedReels == REELS;
}

bool SlotsGame::restore(const State& state) {
  if (!validateState(state)) return false;
  current = state;
  return true;
}
