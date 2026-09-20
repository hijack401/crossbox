#pragma once

#include <cstddef>
#include <cstdint>

class BaccaratGame {
 public:
  static constexpr size_t SHOE_CARDS = 416;
  static constexpr size_t MAX_CARDS = 3;
  static constexpr int64_t MIN_BET_CENTS = 100;
  static constexpr int64_t MAX_BET_CENTS = 99999999900LL;

  enum class Bet : uint8_t { Player, Banker, Tie };
  enum class Phase : uint8_t { Betting = 0, Settled = 1, Revealing = 2 };

  struct Hand {
    uint8_t cards[MAX_CARDS]{};
    uint8_t cardCount = 0;
  };

  struct State {
    int64_t wagerCents = 0;
    int64_t returnCents = 0;
    uint8_t shoe[SHOE_CARDS]{};
    uint16_t shoePosition = 0;
    uint8_t shoeReady = 0;
    Hand player{};
    Hand banker{};
    Bet bet = Bet::Player;
    Bet winner = Bet::Tie;
    Phase phase = Phase::Betting;
    uint8_t revealedCards = 0;
  };

  using Random = uint32_t (*)(void* context);

  const State& state() const { return current; }
  bool restore(const State& state);
  static bool validateState(const State& state);
  void reset();

  bool canBet(int64_t wagerCents, int64_t availableCents) const;
  bool startRound(Bet bet, int64_t wagerCents, int64_t availableCents, Random random, void* context = nullptr);
  bool revealNext();
  bool cardRevealed(bool banker, uint8_t cardIndex) const;
  bool nextRound();

  static uint8_t cardValue(uint8_t card);
  static uint8_t value(const Hand& hand);
  static bool natural(const Hand& hand);

 private:
  State current{};

  void clearRound();
  void shuffle(Random random, void* context);
  void deal(Hand& hand);
};
