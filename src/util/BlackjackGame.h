#pragma once

#include <cstddef>
#include <cstdint>

class BlackjackGame {
 public:
  static constexpr size_t SHOE_CARDS = 312;
  static constexpr size_t MAX_HANDS = 4;
  static constexpr size_t MAX_CARDS = 22;
  static constexpr int64_t STARTING_BALANCE_CENTS = 100000;
  static constexpr int64_t DAILY_CREDIT_CENTS = 10000;
  static constexpr int64_t MAX_BALANCE_CENTS = 99999999900LL;
  static constexpr int64_t MIN_BET_CENTS = 100;
  static constexpr int32_t MAX_CREDIT_DAY = 3000000;

  enum class Phase : uint8_t { Betting, Insurance, Playing, Settled };
  enum class Result : uint8_t { Pending, Win, Lose, Push, Blackjack, Bust, Surrender };

  struct Hand {
    int64_t wagerCents = 0;
    int64_t returnCents = 0;
    uint8_t cards[MAX_CARDS]{};
    uint8_t cardCount = 0;
    uint8_t finished = 0;
    uint8_t fromSplit = 0;
    uint8_t splitAces = 0;
    uint8_t doubled = 0;
    Result result = Result::Pending;
  };

  struct State {
    int64_t balanceCents = STARTING_BALANCE_CENTS;
    int64_t roundWagerCents = 0;
    int64_t roundReturnCents = 0;
    int64_t insuranceCents = 0;
    int64_t insuranceReturnCents = 0;
    int32_t lastCreditDay = 0;
    uint8_t shoe[SHOE_CARDS]{};
    uint16_t shoePosition = 0;
    uint8_t shoeReady = 0;
    Hand hands[MAX_HANDS]{};
    Hand dealer{};
    uint8_t handCount = 0;
    uint8_t activeHand = 0;
    Phase phase = Phase::Betting;
  };

  using Random = uint32_t (*)(void* context);

  const State& state() const { return current; }
  bool restore(const State& state);
  static bool validateState(const State& state);
  void reset();

  bool canBet(int64_t betCents) const;
  bool startRound(int64_t betCents, Random random, void* context = nullptr);
  bool hit();
  bool stand();
  bool doubleDown();
  bool split();
  bool surrender();
  bool insurance(bool take);
  bool nextRound();
  bool applyDailyCredit(int32_t day);

  bool canHit() const;
  bool canStand() const;
  bool canDouble() const;
  bool canSplit() const;
  bool canSurrender() const;
  bool canInsure() const;

  static uint8_t rank(uint8_t card) { return card % 13 + 1; }
  static uint8_t suit(uint8_t card) { return card / 13; }
  static uint8_t cardValue(uint8_t card);
  static uint8_t value(const Hand& hand);
  static bool isSoft(const Hand& hand);
  static bool natural(const Hand& hand);

 private:
  // Reserving every possible hand's capacity prevents a mid-round reshuffle.
  static constexpr size_t ROUND_RESERVE = (MAX_HANDS + 1) * MAX_CARDS;
  State current{};

  void shuffle(Random random, void* context);
  void clearRound();
  void deal(Hand& hand);
  void afterPeek();
  void advance();
  void settle();
  void credit(int64_t cents);
};
