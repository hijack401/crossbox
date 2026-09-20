#pragma once

#include <cstddef>
#include <cstdint>

class SlotsGame {
 public:
  static constexpr size_t REELS = 3;
  static constexpr int64_t MIN_BET_CENTS = 100;
  static constexpr int64_t MAX_BET_CENTS = 99999999900LL;
  static constexpr uint8_t MAX_RETURN_MULTIPLIER = 100;

  enum class Symbol : uint8_t { Cherry, Lemon, Bell, Bar, Seven };
  enum class Phase : uint8_t { Betting, Revealing, Settled };

  struct State {
    int64_t wagerCents = 0;
    int64_t returnCents = 0;
    Symbol reels[REELS]{Symbol::Cherry, Symbol::Cherry, Symbol::Cherry};
    uint8_t revealedReels = 0;
    Phase phase = Phase::Betting;
  };

  using Random = uint32_t (*)(void* context);

  const State& state() const { return current; }
  bool restore(const State& state);
  static bool validateState(const State& state);
  void reset();

  bool canBet(int64_t wagerCents, int64_t availableCents) const;
  bool startRound(int64_t wagerCents, int64_t availableCents, Random random, void* context = nullptr);
  bool revealNext();
  bool nextRound();
  bool reelRevealed(size_t index) const;

  // Stops 0–19 map to the weighted reel; an invalid stop returns an invalid symbol.
  static Symbol symbolForStop(uint8_t stop);
  // Total return, including the original stake.
  static uint8_t returnMultiplier(const Symbol reels[REELS]);

 private:
  State current{};
};
