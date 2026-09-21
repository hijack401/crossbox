#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <type_traits>

#include "util/FarkleGame.h"

namespace {
using Game = FarkleGame;
using Phase = Game::Phase;
using Player = Game::Player;
using Reason = Game::EndReason;

struct Samples {
  std::array<uint32_t, 32> values{};
  size_t size = 0;
  size_t position = 0;

  Samples(std::initializer_list<uint8_t> faces) {
    for (const auto face : faces) values[size++] = 6 + face - 1;
  }
};

uint32_t randomSample(void* context) {
  auto& samples = *static_cast<Samples*>(context);
  EXPECT_LT(samples.position, samples.size);
  return samples.values[samples.position++ % samples.values.size()];
}

bool roll(Game& game, std::initializer_list<uint8_t> faces) {
  Samples samples(faces);
  const bool result = game.roll(randomSample, &samples);
  EXPECT_EQ(samples.position, result ? samples.size : 0);
  if (result) EXPECT_TRUE(Game::validateState(game.state()));
  return result;
}

bool continueRoll(Game& game, uint8_t mask, std::initializer_list<uint8_t> faces) {
  Samples samples(faces);
  const bool result = game.holdAndRoll(mask, randomSample, &samples);
  EXPECT_EQ(samples.position, result ? samples.size : 0);
  if (result) EXPECT_TRUE(Game::validateState(game.state()));
  return result;
}

uint32_t score(std::initializer_list<uint8_t> faces) {
  uint8_t dice[Game::DICE]{};
  uint8_t mask = 0;
  size_t index = 0;
  for (auto face : faces) {
    dice[index] = face;
    mask |= 1 << index++;
  }
  return Game::scoreDice(dice, mask);
}

void unchanged(const Game& game, const Game::State& before) {
  const auto& now = game.state();
  EXPECT_EQ(now.wagerCents, before.wagerCents);
  EXPECT_EQ(now.returnCents, before.returnCents);
  EXPECT_EQ(now.scores[0], before.scores[0]);
  EXPECT_EQ(now.scores[1], before.scores[1]);
  EXPECT_EQ(now.turnPoints, before.turnPoints);
  EXPECT_EQ(now.targetScore, before.targetScore);
  EXPECT_EQ(now.heldMask, before.heldMask);
  EXPECT_EQ(now.rolledMask, before.rolledMask);
  EXPECT_EQ(now.activePlayer, before.activePlayer);
  EXPECT_EQ(now.phase, before.phase);
  EXPECT_EQ(now.endReason, before.endReason);
  for (size_t i = 0; i < Game::DICE; ++i) EXPECT_EQ(now.dice[i], before.dice[i]);
}

void reject(const Game::State& state) {
  Game game;
  ASSERT_TRUE(game.startMatch(2000, 5000));
  const auto before = game.state();
  EXPECT_FALSE(Game::validateState(state));
  EXPECT_FALSE(game.restore(state));
  unchanged(game, before);
}

// Independent scorer partitions a throw into scoring groups from the printed table.
int32_t referenceScore(const uint8_t dice[Game::DICE], const uint8_t mask) {
  if (!mask) return 0;
  uint8_t firstBit = 1;
  while (!(mask & firstBit)) firstBit <<= 1;
  int32_t best = -1;
  for (uint8_t group = mask; group; group = (group - 1) & mask) {
    if (!(group & firstBit)) continue;
    std::array<uint8_t, Game::DICE> values{};
    size_t count = 0;
    for (size_t i = 0; i < Game::DICE; ++i)
      if (group & (1 << i)) values[count++] = dice[i];
    std::sort(values.begin(), values.begin() + count);
    int32_t points = 0;
    if (count == 1 && values[0] == 1) points = 100;
    if (count == 1 && values[0] == 5) points = 50;
    if (count >= 3 && values.front() == values[count - 1]) {
      static constexpr int32_t TABLE[6][4] = {{1000, 2000, 4000, 8000}, {200, 400, 800, 1600},
                                              {300, 600, 1200, 2400},   {400, 800, 1600, 3200},
                                              {500, 1000, 2000, 4000},  {600, 1200, 2400, 4800}};
      points = TABLE[values[0] - 1][count - 3];
    }
    if (count >= 5) {
      bool consecutive = true;
      for (size_t i = 1; i < count; ++i) consecutive &= values[i] == values[0] + i;
      if (consecutive) points = count == 6 ? 1500 : values[0] == 1 ? 500 : 750;
    }
    if (!points) continue;
    const int32_t rest = referenceScore(dice, mask & ~group);
    if (rest >= 0) best = std::max(best, points + rest);
  }
  return best;
}
}  // namespace

TEST(FarkleGame, FixedStorageDefaultStateAndPhaseGuards) {
  static_assert(std::is_trivially_copyable_v<Game::State>);
  static_assert(sizeof(Game::State) <= 64);
  static_assert(sizeof(Game) == sizeof(Game::State));
  Game game;
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_EQ(game.state().phase, Phase::Betting);
  EXPECT_EQ(game.state().targetScore, 4000);
  EXPECT_FALSE(game.roll(nullptr));
  EXPECT_FALSE(game.holdAndRoll(1, nullptr));
  EXPECT_FALSE(game.bank(1));
  EXPECT_FALSE(game.nextTurn());
  EXPECT_FALSE(game.nextMatch());
  EXPECT_EQ(game.scoreSelection(1), 0);
  EXPECT_EQ(game.bestSelection(), 0);
  EXPECT_FALSE(game.computerShouldBank(1));
}

TEST(FarkleGame, ExactSinglesTriplesAndDoublingPaytable) {
  EXPECT_EQ(score({1}), 100);
  EXPECT_EQ(score({5}), 50);
  EXPECT_EQ(score({1, 1}), 200);
  EXPECT_EQ(score({5, 5}), 100);
  EXPECT_EQ(score({1, 5}), 150);
  for (uint8_t face = 1; face <= 6; ++face) {
    const uint8_t dice[]{face, face, face, face, face, face};
    const uint32_t triple = face == 1 ? 1000 : face * 100;
    EXPECT_EQ(Game::scoreDice(dice, 7), triple);
    EXPECT_EQ(Game::scoreDice(dice, 15), triple * 2);
    EXPECT_EQ(Game::scoreDice(dice, 31), triple * 4);
    EXPECT_EQ(Game::scoreDice(dice, 63), triple * 8);
    if (face != 1 && face != 5) {
      EXPECT_EQ(Game::scoreDice(dice, 1), 0);
      EXPECT_EQ(Game::scoreDice(dice, 3), 0);
    }
  }
  EXPECT_EQ(score({1, 1, 1, 5, 5, 5}), 1500);
  EXPECT_EQ(score({2, 2, 2, 4, 4, 4}), 600);
  EXPECT_EQ(score({2, 2, 3, 3, 4, 4}), 0);
  EXPECT_EQ(score({1, 1, 5, 5, 6, 6}), 0);
}

TEST(FarkleGame, StraightsCanCombineOnlyWithAdditionalScoringDice) {
  EXPECT_EQ(score({1, 2, 3, 4, 5}), 500);
  EXPECT_EQ(score({2, 3, 4, 5, 6}), 750);
  EXPECT_EQ(score({1, 2, 3, 4, 5, 6}), 1500);
  EXPECT_EQ(score({1, 2, 3, 4, 5, 5}), 550);
  EXPECT_EQ(score({1, 1, 2, 3, 4, 5}), 600);
  EXPECT_EQ(score({2, 3, 4, 5, 5, 6}), 800);
  EXPECT_EQ(score({2, 3, 4, 5, 6, 6}), 0);
  EXPECT_EQ(score({1, 2, 2, 3, 4, 5}), 0);
}

TEST(FarkleGame, EveryOrderedThrowOfOneThroughSixDiceMatchesIndependentScoringPartitions) {
  uint8_t dice[Game::DICE]{};
  uint32_t combinations = 1;
  for (uint8_t count = 1; count <= Game::DICE; ++count) {
    combinations *= 6;
    const uint8_t mask = (1 << count) - 1;
    for (uint32_t code = 0; code < combinations; ++code) {
      uint32_t remaining = code;
      for (uint8_t i = 0; i < count; ++i) {
        dice[i] = remaining % 6 + 1;
        remaining /= 6;
      }
      const uint32_t expected = std::max(0, referenceScore(dice, mask));
      ASSERT_EQ(Game::scoreDice(dice, mask), expected) << "count=" << +count << " throw=" << code;
    }
  }
}

TEST(FarkleGame, ScoreRejectsEmptyMasksOutOfRangeDiceAndUnscoringMixtures) {
  const uint8_t dice[]{1, 2, 3, 4, 5, 6};
  EXPECT_EQ(Game::scoreDice(nullptr, 1), 0);
  EXPECT_EQ(Game::scoreDice(dice, 0), 0);
  EXPECT_EQ(Game::scoreDice(dice, 0x80), 0);
  EXPECT_EQ(Game::scoreDice(dice, 0xff), 0);
  EXPECT_EQ(Game::scoreDice(dice, 3), 0);
  EXPECT_EQ(score({0}), 0);
  EXPECT_EQ(score({7}), 0);
  EXPECT_EQ(score({255}), 0);
}

TEST(FarkleGame, WagersAndTargetsValidateBeforeMutatingMatch) {
  Game game;
  const auto initial = game.state();
  for (const auto amount : {-100LL, 0LL, 1LL, 99LL, 101LL, 1100LL, 99999999901LL, INT64_MAX}) {
    EXPECT_FALSE(game.startMatch(amount, 1000));
    unchanged(game, initial);
  }
  for (const auto balance : {-1LL, 0LL, 99LL, 99999999901LL, INT64_MAX}) {
    EXPECT_FALSE(game.startMatch(100, balance));
    unchanged(game, initial);
  }
  for (const uint32_t target : {0u, 500u, 999u, 1001u, 10500u, UINT32_MAX}) {
    EXPECT_FALSE(Game::validTargetScore(target));
    EXPECT_FALSE(game.startMatch(100, 1000, target));
    unchanged(game, initial);
  }
  for (uint32_t target = 1000; target <= 10000; target += 500) EXPECT_TRUE(Game::validTargetScore(target));
  EXPECT_TRUE(game.startMatch(Game::MAX_BET_CENTS, Game::MAX_BET_CENTS, 1500));
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_EQ(game.state().activePlayer, Player::You);
  EXPECT_EQ(game.state().phase, Phase::AwaitRoll);
  EXPECT_FALSE(game.startMatch(100, 1000));
}

TEST(FarkleGame, InvalidSelectionAndNullRandomLeaveStateUntouched) {
  Game game;
  ASSERT_TRUE(game.startMatch(1000, 10000));
  const auto waiting = game.state();
  EXPECT_FALSE(game.roll(nullptr));
  unchanged(game, waiting);
  ASSERT_TRUE(roll(game, {1, 2, 3, 4, 5, 6}));
  const auto selecting = game.state();
  for (uint8_t mask : {0, 2, 3, 64, 255}) {
    EXPECT_FALSE(game.bank(mask));
    EXPECT_FALSE(game.holdAndRoll(mask, nullptr));
    EXPECT_EQ(game.scoreSelection(mask), 0);
    unchanged(game, selecting);
  }
  EXPECT_FALSE(game.holdAndRoll(1, nullptr));
  EXPECT_FALSE(game.roll(nullptr));
  EXPECT_FALSE(game.nextTurn());
  EXPECT_FALSE(game.nextMatch());
  unchanged(game, selecting);
}

TEST(FarkleGame, HeldDiceCannotBeReusedOrCombinedWithLaterThrows) {
  Game game;
  ASSERT_TRUE(game.startMatch(1000, 10000));
  ASSERT_TRUE(roll(game, {2, 2, 2, 1, 3, 4}));
  ASSERT_TRUE(continueRoll(game, 7, {2, 1, 5}));
  EXPECT_EQ(game.state().heldMask, 7);
  EXPECT_EQ(game.state().rolledMask, 56);
  EXPECT_EQ(game.state().turnPoints, 200);
  EXPECT_EQ(game.scoreSelection(8), 0);
  EXPECT_EQ(game.scoreSelection(15), 0);
  EXPECT_EQ(game.scoreSelection(7), 0);
  EXPECT_EQ(game.scoreSelection(48), 150);
  EXPECT_EQ(game.bestSelection(), 48);
  ASSERT_TRUE(game.bank(48));
  EXPECT_EQ(game.state().scores[0], 350);
  EXPECT_EQ(game.state().turnPoints, 350);
  EXPECT_EQ(game.state().endReason, Reason::Banked);
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_FALSE(game.bank(48));
  ASSERT_TRUE(game.nextTurn());
  EXPECT_EQ(game.state().activePlayer, Player::Opponent);
  EXPECT_EQ(game.state().scores[0], 350);
  EXPECT_EQ(game.state().turnPoints, 0);
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(FarkleGame, HotDiceRerollAllSixAndKeepTurnPoints) {
  Game game;
  ASSERT_TRUE(game.startMatch(1000, 10000));
  ASSERT_TRUE(roll(game, {1, 2, 3, 4, 5, 6}));
  EXPECT_EQ(game.bestSelection(), 63);
  ASSERT_TRUE(continueRoll(game, 63, {1, 5, 2, 3, 4, 6}));
  EXPECT_EQ(game.state().heldMask, 0);
  EXPECT_EQ(game.state().rolledMask, 63);
  EXPECT_EQ(game.state().turnPoints, 1500);
  ASSERT_TRUE(game.bank(3));
  EXPECT_EQ(game.state().scores[0], 1650);
  EXPECT_EQ(game.state().phase, Phase::TurnEnded);
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(FarkleGame, BustForfeitsOnlyUnbankedTurnAndThreePairsDoNotScore) {
  Game game;
  ASSERT_TRUE(game.startMatch(1000, 10000));
  ASSERT_TRUE(roll(game, {1, 2, 3, 4, 5, 6}));
  ASSERT_TRUE(game.bank(1));
  ASSERT_TRUE(game.nextTurn());
  ASSERT_TRUE(roll(game, {2, 2, 3, 3, 4, 4}));
  EXPECT_EQ(game.state().phase, Phase::TurnEnded);
  EXPECT_EQ(game.state().endReason, Reason::Bust);
  ASSERT_TRUE(game.nextTurn());
  ASSERT_TRUE(roll(game, {1, 2, 3, 4, 5, 6}));
  ASSERT_TRUE(continueRoll(game, 1, {2, 2, 3, 3, 6}));
  EXPECT_EQ(game.state().phase, Phase::TurnEnded);
  EXPECT_EQ(game.state().endReason, Reason::Bust);
  EXPECT_EQ(game.state().turnPoints, 0);
  EXPECT_EQ(game.state().scores[0], 100);
  EXPECT_EQ(game.state().scores[1], 0);
  EXPECT_EQ(game.state().returnCents, 0);
  EXPECT_FALSE(game.bank(1));
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(FarkleGame, BankingTargetWinsImmediatelyAndPaysStakePlusMatchingOpponentStake) {
  for (const auto wager : {100LL, 4000LL, Game::MAX_BET_CENTS}) {
    Game game;
    ASSERT_TRUE(game.startMatch(wager, Game::MAX_BET_CENTS, 1000));
    ASSERT_TRUE(roll(game, {1, 1, 1, 2, 3, 4}));
    ASSERT_TRUE(game.bank(7));
    EXPECT_EQ(game.state().phase, Phase::Settled);
    EXPECT_EQ(game.state().scores[0], 1000);
    EXPECT_EQ(game.state().scores[1], 0);
    EXPECT_EQ(game.state().returnCents, wager * 2);
    EXPECT_TRUE(Game::validateState(game.state()));
    EXPECT_FALSE(game.nextTurn());
    EXPECT_FALSE(game.bank(7));
    ASSERT_TRUE(game.nextMatch());
    EXPECT_TRUE(Game::validateState(game.state()));
    EXPECT_EQ(game.state().phase, Phase::Betting);
  }
}

TEST(FarkleGame, OpponentCanWinAndUnbankedTargetDoesNotEndMatch) {
  Game game;
  ASSERT_TRUE(game.startMatch(1000, 10000, 1000));
  ASSERT_TRUE(roll(game, {1, 1, 1, 2, 3, 4}));
  ASSERT_TRUE(continueRoll(game, 7, {2, 3, 4}));
  EXPECT_EQ(game.state().phase, Phase::TurnEnded);
  EXPECT_EQ(game.state().scores[0], 0);
  ASSERT_TRUE(game.nextTurn());
  ASSERT_TRUE(roll(game, {1, 2, 3, 4, 5, 6}));
  ASSERT_TRUE(game.bank(63));
  EXPECT_EQ(game.state().phase, Phase::Settled);
  EXPECT_EQ(game.state().activePlayer, Player::Opponent);
  EXPECT_EQ(game.state().scores[1], 1500);
  EXPECT_EQ(game.state().returnCents, 0);
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_TRUE(game.startMatch(500, 500));
}

TEST(FarkleGame, RandomSamplingRejectsBiasedTailAndExhaustionIsAtomic) {
  Game game;
  ASSERT_TRUE(game.startMatch(1000, 10000));
  Samples samples({});
  samples.values = {0, 1, 2, 3, 6, 7, 8, 9, 10, 11};
  samples.size = 10;
  ASSERT_TRUE(game.roll(randomSample, &samples));
  EXPECT_EQ(samples.position, 10);
  for (size_t i = 0; i < Game::DICE; ++i) EXPECT_EQ(game.state().dice[i], i + 1);
  const auto before = game.state();
  const auto zero = [](void* context) -> uint32_t {
    ++*static_cast<uint32_t*>(context);
    return 0;
  };
  uint32_t calls = 0;
  EXPECT_FALSE(game.holdAndRoll(63, zero, &calls));
  EXPECT_EQ(calls, 128);
  unchanged(game, before);
  Game waiting;
  ASSERT_TRUE(waiting.startMatch(1000, 10000));
  const auto waitingBefore = waiting.state();
  calls = 0;
  EXPECT_FALSE(waiting.roll(zero, &calls));
  EXPECT_EQ(calls, 128);
  unchanged(waiting, waitingBefore);
}

TEST(FarkleGame, BestSelectionFindsStraightsAndExcludesNonScoringRemainder) {
  Game game;
  ASSERT_TRUE(game.startMatch(1000, 10000));
  ASSERT_TRUE(roll(game, {2, 3, 4, 5, 6, 6}));
  const auto selected = game.bestSelection();
  EXPECT_EQ(game.scoreSelection(selected), 750);
  EXPECT_EQ(game.scoreSelection(63), 0);
  ASSERT_TRUE(game.bank(selected));
  EXPECT_EQ(game.state().scores[0], 750);
}

TEST(FarkleGame, ComputerBanksWinningScoreAndUsesDiceRiskWithoutFutureRandomness) {
  Game game;
  ASSERT_TRUE(game.startMatch(1000, 10000, 1000));
  ASSERT_TRUE(roll(game, {1, 1, 1, 2, 3, 4}));
  EXPECT_TRUE(game.computerShouldBank(7));
  Game risky;
  ASSERT_TRUE(risky.startMatch(1000, 10000));
  ASSERT_TRUE(roll(risky, {1, 1, 5, 5, 5, 2}));
  EXPECT_TRUE(risky.computerShouldBank(31));
  EXPECT_FALSE(risky.computerShouldBank(32));
  Game fresh;
  ASSERT_TRUE(fresh.startMatch(1000, 10000));
  ASSERT_TRUE(roll(fresh, {1, 2, 2, 3, 4, 6}));
  EXPECT_FALSE(fresh.computerShouldBank(1));
  Game hot;
  ASSERT_TRUE(hot.startMatch(1000, 10000));
  ASSERT_TRUE(roll(hot, {2, 2, 2, 3, 3, 3}));
  EXPECT_FALSE(hot.computerShouldBank(63));
}

TEST(FarkleGame, OverflowingTurnOrBankDoesNotMutateState) {
  Game game;
  ASSERT_TRUE(game.startMatch(1000, 10000));
  ASSERT_TRUE(roll(game, {1, 2, 3, 4, 5, 6}));
  auto state = game.state();
  state.turnPoints = 4294967250u;
  ASSERT_TRUE(game.restore(state));
  EXPECT_FALSE(game.bank(1));
  EXPECT_FALSE(game.holdAndRoll(1, nullptr));
  unchanged(game, state);
  state.turnPoints = 4294967150u;
  state.scores[0] = 100;
  ASSERT_TRUE(game.restore(state));
  EXPECT_FALSE(game.bank(1));
  unchanged(game, state);
}

TEST(FarkleGame, EveryPhaseRoundTripsAndMalformedStatesAreRejectedAtomically) {
  Game game;
  auto roundTrip = [&]() {
    Game restored;
    ASSERT_TRUE(restored.restore(game.state()));
    unchanged(restored, game.state());
  };
  roundTrip();
  auto bad = game.state();
  bad.targetScore = 1500;
  reject(bad);
  bad = game.state();
  bad.dice[0] = 1;
  reject(bad);
  ASSERT_TRUE(game.startMatch(1000, 10000, 1500));
  roundTrip();
  bad = game.state();
  bad.turnPoints = 100;
  reject(bad);
  ASSERT_TRUE(roll(game, {1, 2, 3, 4, 5, 6}));
  roundTrip();
  const auto selecting = game.state();
  bad = selecting;
  bad.phase = static_cast<Phase>(255);
  reject(bad);
  bad = selecting;
  bad.activePlayer = static_cast<Player>(255);
  reject(bad);
  bad = selecting;
  bad.endReason = static_cast<Reason>(255);
  reject(bad);
  bad = selecting;
  bad.endReason = Reason::Bust;
  reject(bad);
  bad = selecting;
  bad.wagerCents = 101;
  reject(bad);
  bad = selecting;
  bad.returnCents = 2000;
  reject(bad);
  bad = selecting;
  bad.scores[1] = 1500;
  reject(bad);
  bad = selecting;
  bad.turnPoints = 1;
  reject(bad);
  bad = selecting;
  bad.heldMask = 1;
  reject(bad);
  bad = selecting;
  bad.rolledMask = 0;
  reject(bad);
  bad = selecting;
  bad.rolledMask = 255;
  reject(bad);
  bad = selecting;
  bad.dice[0] = 7;
  reject(bad);
  ASSERT_TRUE(game.bank(1));
  roundTrip();
  bad = game.state();
  bad.turnPoints = 150;
  reject(bad);
  ASSERT_TRUE(game.nextTurn());
  ASSERT_TRUE(roll(game, {2, 2, 3, 3, 4, 4}));
  roundTrip();
  bad = game.state();
  bad.dice[0] = 1;
  reject(bad);
  ASSERT_TRUE(game.nextTurn());
  ASSERT_TRUE(roll(game, {1, 2, 3, 4, 5, 6}));
  ASSERT_TRUE(game.bank(63));
  roundTrip();
  bad = game.state();
  bad.returnCents = 1000;
  reject(bad);
  bad = game.state();
  bad.scores[0] = 1000;
  reject(bad);
}

TEST(FarkleGame, SeededComputerMatchesAlwaysPreserveValidStateAndSettleExactlyOnce) {
  for (uint32_t seed = 1; seed <= 100; ++seed) {
    uint32_t randomState = seed;
    const auto random = [](void* context) -> uint32_t {
      auto& state = *static_cast<uint32_t*>(context);
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      return state;
    };
    Game game;
    ASSERT_TRUE(game.startMatch(1000, 10000, 4000));
    unsigned steps = 0;
    while (game.state().phase != Phase::Settled && ++steps < 2000) {
      switch (game.state().phase) {
        case Phase::AwaitRoll:
          ASSERT_TRUE(game.roll(random, &randomState));
          break;
        case Phase::Selecting: {
          const auto selected = game.bestSelection();
          ASSERT_NE(selected, 0);
          if (game.computerShouldBank(selected))
            ASSERT_TRUE(game.bank(selected));
          else
            ASSERT_TRUE(game.holdAndRoll(selected, random, &randomState));
          break;
        }
        case Phase::TurnEnded:
          ASSERT_TRUE(game.nextTurn());
          break;
        default:
          FAIL() << "Unexpected phase";
      }
      ASSERT_TRUE(Game::validateState(game.state())) << "seed=" << seed << " step=" << steps;
      Game restored;
      ASSERT_TRUE(restored.restore(game.state()));
    }
    ASSERT_LT(steps, 2000);
    EXPECT_EQ(game.state().returnCents, game.state().activePlayer == Player::You ? 2000 : 0);
    EXPECT_FALSE(game.bank(63));
    EXPECT_FALSE(game.nextTurn());
  }
}
