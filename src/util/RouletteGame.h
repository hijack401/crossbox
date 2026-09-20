#pragma once

#include <cstddef>
#include <cstdint>

class RouletteGame {
 public:
  static constexpr size_t MAX_BETS = 16;
  static constexpr int64_t MIN_BET_CENTS = 100;
  static constexpr int64_t MAX_BET_CENTS = 99999999900LL;

  enum class Type : uint8_t {
    Straight,
    Split,
    Street,
    Corner,
    SixLine,
    Trio,
    FirstFour,
    Red,
    Black,
    Odd,
    Even,
    Low,
    High,
    Dozen,
    Column
  };
  enum class Phase : uint8_t { Betting, Spinning, Settled };

  struct Bet {
    Type type = Type::Straight;
    uint8_t first = 0;
    uint8_t second = 0;
  };

  struct Entry {
    int64_t amountCents = 0;
    Bet bet{};
  };

  struct State {
    Entry bets[MAX_BETS]{};
    int64_t wagerCents = 0;
    int64_t returnCents = 0;
    uint8_t betCount = 0;
    uint8_t result = 0;
    Phase phase = Phase::Betting;
  };

  using Random = uint32_t (*)(void* context);

  const State& state() const { return current; }
  bool restore(const State& state);
  static bool validateState(const State& state);
  void reset();

  static bool validBet(Bet bet);
  static bool covers(Bet bet, uint8_t result);
  static uint8_t profitOdds(Bet bet);
  static bool isRed(uint8_t result);

  // Split endpoints may be entered in either order; stored bets are canonical.
  bool canAddBet(Bet bet, int64_t amountCents, int64_t availableCents) const;
  bool addBet(Bet bet, int64_t amountCents, int64_t availableCents);
  bool removeBet(size_t index);
  bool clearBets();
  bool canSpin(int64_t availableCents) const;
  bool startRound(int64_t availableCents, Random random, void* context = nullptr);
  bool reveal();
  bool nextRound(bool keepBets = false);

 private:
  State current{};
};
