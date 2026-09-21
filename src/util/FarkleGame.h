#pragma once

#include <cstddef>
#include <cstdint>

class FarkleGame {
 public:
  static constexpr size_t DICE = 6;
  static constexpr uint8_t ALL_DICE_MASK = 0x3f;
  static constexpr int64_t MIN_BET_CENTS = 100;
  static constexpr int64_t MAX_BET_CENTS = 99999999900LL;
  static constexpr uint32_t DEFAULT_TARGET_SCORE = 4000;

  enum class Phase : uint8_t { Betting, AwaitRoll, Selecting, TurnEnded, Settled };
  enum class Player : uint8_t { You, Opponent };
  enum class EndReason : uint8_t { None, Banked, Bust };

  struct State {
    int64_t wagerCents = 0;
    int64_t returnCents = 0;
    uint32_t scores[2]{};
    uint32_t turnPoints = 0;
    uint32_t targetScore = DEFAULT_TARGET_SCORE;
    uint8_t dice[DICE]{};
    uint8_t heldMask = 0;
    uint8_t rolledMask = 0;
    Player activePlayer = Player::You;
    Phase phase = Phase::Betting;
    EndReason endReason = EndReason::None;
  };

  using Random = uint32_t (*)(void* context);

  const State& state() const { return current; }
  bool restore(const State& state);
  static bool validateState(const State& state);
  void reset();

  static bool validTargetScore(uint32_t targetScore);
  bool canBet(int64_t wagerCents, int64_t availableCents) const;
  bool startMatch(int64_t wagerCents, int64_t availableCents, uint32_t targetScore = DEFAULT_TARGET_SCORE);
  bool roll(Random random, void* context = nullptr);

  // Every selected die must score, and all must come from the current throw.
  static uint32_t scoreDice(const uint8_t dice[DICE], uint8_t mask);
  uint32_t scoreSelection(uint8_t mask) const;
  uint8_t bestSelection() const;
  bool holdAndRoll(uint8_t mask, Random random, void* context = nullptr);
  bool bank(uint8_t mask);
  bool nextTurn();
  bool nextMatch();
  bool computerShouldBank(uint8_t mask) const;

 private:
  State current{};

  static bool rollRemaining(State& state, Random random, void* context);
};
