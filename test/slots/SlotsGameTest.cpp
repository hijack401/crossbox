#include <gtest/gtest.h>

#include <array>
#include <type_traits>

#include "util/SlotsGame.h"

namespace {
using Game = SlotsGame;
using Symbol = Game::Symbol;
using Phase = Game::Phase;

struct Stops {
  uint32_t values[Game::REELS]{};
  size_t position = 0;
};

uint32_t nextStop(void* context) {
  auto& stops = *static_cast<Stops*>(context);
  EXPECT_LT(stops.position, Game::REELS);
  return stops.values[stops.position++ % Game::REELS];
}

bool spin(Game& game, uint8_t first, uint8_t second, uint8_t third, int64_t wager = 100,
          int64_t available = Game::MAX_BET_CENTS) {
  Stops stops{
      {static_cast<uint32_t>(20 + first), static_cast<uint32_t>(20 + second), static_cast<uint32_t>(20 + third)}};
  const bool result = game.startRound(wager, available, nextStop, &stops);
  EXPECT_EQ(stops.position, result ? Game::REELS : 0);
  return result;
}

Symbol expectedSymbol(uint8_t stop) {
  static constexpr Symbol REEL[] = {Symbol::Cherry, Symbol::Cherry, Symbol::Cherry, Symbol::Cherry, Symbol::Cherry,
                                    Symbol::Cherry, Symbol::Lemon,  Symbol::Lemon,  Symbol::Lemon,  Symbol::Lemon,
                                    Symbol::Lemon,  Symbol::Bell,   Symbol::Bell,   Symbol::Bell,   Symbol::Bell,
                                    Symbol::Bar,    Symbol::Bar,    Symbol::Bar,    Symbol::Seven,  Symbol::Seven};
  return REEL[stop];
}

uint8_t expectedReturn(Symbol first, Symbol second, Symbol third) {
  if (first == Symbol::Seven && second == Symbol::Seven && third == Symbol::Seven) return 100;
  if (first == Symbol::Bar && second == Symbol::Bar && third == Symbol::Bar) return 30;
  if (first == Symbol::Bell && second == Symbol::Bell && third == Symbol::Bell) return 15;
  if (first == Symbol::Lemon && second == Symbol::Lemon && third == Symbol::Lemon) return 8;
  if (first == Symbol::Cherry && second == Symbol::Cherry && third == Symbol::Cherry) return 5;
  if ((first == Symbol::Cherry && second == Symbol::Cherry) || (first == Symbol::Cherry && third == Symbol::Cherry) ||
      (second == Symbol::Cherry && third == Symbol::Cherry))
    return 2;
  return 0;
}

void unchanged(const Game& game, const Game::State& before) {
  const auto& actual = game.state();
  EXPECT_EQ(actual.phase, before.phase);
  EXPECT_EQ(actual.wagerCents, before.wagerCents);
  EXPECT_EQ(actual.returnCents, before.returnCents);
  EXPECT_EQ(actual.revealedReels, before.revealedReels);
  for (size_t i = 0; i < Game::REELS; ++i) EXPECT_EQ(actual.reels[i], before.reels[i]);
}

void reject(const Game::State& state) {
  Game game;
  ASSERT_TRUE(spin(game, 0, 6, 11));
  const auto before = game.state();
  EXPECT_FALSE(Game::validateState(state));
  EXPECT_FALSE(game.restore(state));
  unchanged(game, before);
}
}  // namespace

TEST(SlotsGame, DefaultStateIsEmptyAndUsesFixedStorage) {
  static_assert(std::is_trivially_copyable_v<Game::State>);
  static_assert(sizeof(Game::State) == 24);
  static_assert(sizeof(Game) == sizeof(Game::State));
  Game game;
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_EQ(game.state().phase, Phase::Betting);
  EXPECT_EQ(game.state().wagerCents, 0);
  EXPECT_EQ(game.state().returnCents, 0);
  EXPECT_EQ(game.state().revealedReels, 0);
  for (size_t i = 0; i < Game::REELS; ++i) {
    EXPECT_EQ(game.state().reels[i], Symbol::Cherry);
    EXPECT_FALSE(game.reelRevealed(i));
  }
  EXPECT_FALSE(game.revealNext());
  EXPECT_FALSE(game.nextRound());
}

TEST(SlotsGame, TwentyStopsHaveExactSymbolWeightsAndRejectOutOfRangeStops) {
  std::array<uint8_t, 5> counts{};
  for (uint8_t stop = 0; stop < 20; ++stop) {
    const auto symbol = Game::symbolForStop(stop);
    EXPECT_EQ(symbol, expectedSymbol(stop));
    ++counts[static_cast<uint8_t>(symbol)];
  }
  EXPECT_EQ(counts, (std::array<uint8_t, 5>{6, 5, 4, 3, 2}));
  for (unsigned stop = 20; stop <= 255; ++stop)
    EXPECT_GT(static_cast<uint8_t>(Game::symbolForStop(stop)), static_cast<uint8_t>(Symbol::Seven));
}

TEST(SlotsGame, AllEightThousandStopCombinationsMatchIndependentPaytableAndExactReturn) {
  uint64_t returnedUnits = 0;
  unsigned combinations = 0;
  unsigned jackpotCount = 0;
  unsigned cherryPairCount = 0;
  for (uint8_t first = 0; first < 20; ++first) {
    for (uint8_t second = 0; second < 20; ++second) {
      for (uint8_t third = 0; third < 20; ++third) {
        SCOPED_TRACE(testing::Message() << +first << ',' << +second << ',' << +third);
        const auto expected = expectedReturn(expectedSymbol(first), expectedSymbol(second), expectedSymbol(third));
        Game game;
        ASSERT_TRUE(spin(game, first, second, third));
        const auto& state = game.state();
        ASSERT_EQ(state.phase, Phase::Revealing);
        ASSERT_EQ(state.revealedReels, 0);
        ASSERT_EQ(state.reels[0], expectedSymbol(first));
        ASSERT_EQ(state.reels[1], expectedSymbol(second));
        ASSERT_EQ(state.reels[2], expectedSymbol(third));
        ASSERT_EQ(state.returnCents, expected * 100);
        ASSERT_TRUE(Game::validateState(state));
        for (size_t i = 0; i < Game::REELS; ++i) {
          ASSERT_TRUE(game.revealNext());
          ASSERT_TRUE(Game::validateState(game.state()));
        }
        ASSERT_EQ(game.state().phase, Phase::Settled);
        ASSERT_EQ(game.state().returnCents, expected * 100);
        returnedUnits += expected;
        jackpotCount += expected == 100;
        cherryPairCount += expected == 2;
        ++combinations;
      }
    }
  }
  EXPECT_EQ(combinations, 8000);
  EXPECT_EQ(jackpotCount, 8);
  EXPECT_EQ(cherryPairCount, 1512);
  EXPECT_EQ(returnedUnits, 7674);
  EXPECT_DOUBLE_EQ(static_cast<double>(returnedUnits) / combinations * 100, 95.925);
}

TEST(SlotsGame, AllOneHundredTwentyFiveSymbolLinesHaveCorrectTotalReturnMultiplier) {
  for (uint8_t first = 0; first < 5; ++first) {
    for (uint8_t second = 0; second < 5; ++second) {
      for (uint8_t third = 0; third < 5; ++third) {
        const Symbol symbols[]{static_cast<Symbol>(first), static_cast<Symbol>(second), static_cast<Symbol>(third)};
        EXPECT_EQ(Game::returnMultiplier(symbols), expectedReturn(symbols[0], symbols[1], symbols[2]));
      }
    }
  }
}

TEST(SlotsGame, ExactlyTwoCherriesPayInEveryPositionButOtherPairsAndSingleCherriesLose) {
  for (uint8_t other = 1; other < 5; ++other) {
    for (size_t position = 0; position < Game::REELS; ++position) {
      Symbol pair[]{Symbol::Cherry, Symbol::Cherry, Symbol::Cherry};
      pair[position] = static_cast<Symbol>(other);
      EXPECT_EQ(Game::returnMultiplier(pair), 2);
      Symbol single[]{static_cast<Symbol>(other), static_cast<Symbol>(other), static_cast<Symbol>(other)};
      single[position] = Symbol::Cherry;
      EXPECT_EQ(Game::returnMultiplier(single), 0);
    }
  }
  const Symbol mixed[]{Symbol::Lemon, Symbol::Bell, Symbol::Bar};
  EXPECT_EQ(Game::returnMultiplier(mixed), 0);
}

TEST(SlotsGame, TriplesIncludeStakeAndRespectMaximumJackpotWithoutOverflow) {
  static constexpr uint8_t STOPS[]{0, 6, 11, 15, 18};
  static constexpr uint8_t MULTIPLIERS[]{5, 8, 15, 30, 100};
  for (size_t symbol = 0; symbol < 5; ++symbol) {
    for (const int64_t wager : {100LL, 2300LL, 99999999900LL}) {
      Game game;
      ASSERT_TRUE(spin(game, STOPS[symbol], STOPS[symbol], STOPS[symbol], wager));
      EXPECT_EQ(game.state().returnCents, wager * MULTIPLIERS[symbol]);
      EXPECT_TRUE(Game::validateState(game.state()));
    }
  }
  Game jackpot;
  ASSERT_TRUE(spin(jackpot, 18, 19, 18, Game::MAX_BET_CENTS));
  EXPECT_EQ(jackpot.state().returnCents, 9999999990000LL);
}

TEST(SlotsGame, InvalidWagersFundsAndMissingRandomSourceLeaveCurrentStateUntouched) {
  for (bool settled : {false, true}) {
    Game game;
    if (settled) {
      ASSERT_TRUE(spin(game, 18, 18, 18));
      for (size_t i = 0; i < Game::REELS; ++i) ASSERT_TRUE(game.revealNext());
    }
    const auto before = game.state();
    for (const int64_t wager : {-100LL, 0LL, 1LL, 99LL, 101LL, 1100LL, 99999999901LL, INT64_MAX}) {
      EXPECT_FALSE(game.canBet(wager, 1000));
      EXPECT_FALSE(spin(game, 0, 0, 0, wager, 1000));
      unchanged(game, before);
    }
    for (const int64_t available : {-1LL, 0LL, 99LL, 99999999901LL, INT64_MAX}) {
      EXPECT_FALSE(game.canBet(100, available));
      EXPECT_FALSE(spin(game, 0, 0, 0, 100, available));
      unchanged(game, before);
    }
    EXPECT_FALSE(game.startRound(100, 1000, nullptr));
    unchanged(game, before);
    EXPECT_TRUE(game.canBet(100, 199));
    EXPECT_TRUE(game.canBet(Game::MAX_BET_CENTS, Game::MAX_BET_CENTS));
  }
}

TEST(SlotsGame, EachRevealExposesExactlyOneFixedReelAndOnlyThirdSettles) {
  Game game;
  ASSERT_TRUE(spin(game, 0, 6, 18));
  const auto dealt = game.state();
  for (size_t reveal = 0; reveal <= Game::REELS; ++reveal) {
    for (size_t reel = 0; reel < Game::REELS; ++reel) {
      EXPECT_EQ(game.reelRevealed(reel), reel < reveal);
      EXPECT_EQ(game.state().reels[reel], dealt.reels[reel]);
    }
    EXPECT_FALSE(game.reelRevealed(Game::REELS));
    EXPECT_FALSE(game.reelRevealed(SIZE_MAX));
    EXPECT_EQ(game.state().revealedReels, reveal);
    EXPECT_EQ(game.state().phase, reveal == Game::REELS ? Phase::Settled : Phase::Revealing);
    EXPECT_EQ(game.state().returnCents, dealt.returnCents);
    if (reveal < Game::REELS) ASSERT_TRUE(game.revealNext());
  }
  const auto settled = game.state();
  EXPECT_FALSE(game.revealNext());
  unchanged(game, settled);
}

TEST(SlotsGame, CannotChangeWagerOrAbandonPartiallyRevealedSpin) {
  Game game;
  ASSERT_TRUE(spin(game, 0, 6, 11));
  for (size_t reveal = 0; reveal < Game::REELS; ++reveal) {
    const auto before = game.state();
    EXPECT_FALSE(game.canBet(100, 1000));
    EXPECT_FALSE(spin(game, 18, 18, 18));
    EXPECT_FALSE(game.nextRound());
    unchanged(game, before);
    ASSERT_TRUE(game.revealNext());
  }
}

TEST(SlotsGame, SettledSpinCanStartNextSpinDirectlyWithFreshWagerAndRevealPosition) {
  Game game;
  ASSERT_TRUE(spin(game, 18, 18, 18, 100));
  for (size_t i = 0; i < Game::REELS; ++i) ASSERT_TRUE(game.revealNext());
  ASSERT_TRUE(game.canBet(2500, 2500));
  ASSERT_TRUE(spin(game, 0, 6, 11, 2500, 2500));
  EXPECT_EQ(game.state().phase, Phase::Revealing);
  EXPECT_EQ(game.state().wagerCents, 2500);
  EXPECT_EQ(game.state().returnCents, 0);
  EXPECT_EQ(game.state().revealedReels, 0);
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(SlotsGame, NextRoundOnlyClearsASettledOutcome) {
  Game game;
  EXPECT_FALSE(game.nextRound());
  ASSERT_TRUE(spin(game, 0, 0, 6));
  EXPECT_FALSE(game.nextRound());
  for (size_t i = 0; i < Game::REELS; ++i) ASSERT_TRUE(game.revealNext());
  ASSERT_TRUE(game.nextRound());
  Game fresh;
  unchanged(game, fresh.state());
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_FALSE(game.nextRound());
}

TEST(SlotsGame, RestartAtEveryRevealPreservesHiddenSymbolsPayoutAndCompletionGuard) {
  for (size_t revealed = 0; revealed <= Game::REELS; ++revealed) {
    Game original;
    ASSERT_TRUE(spin(original, 18, 18, 18, 2300));
    for (size_t i = 0; i < revealed; ++i) ASSERT_TRUE(original.revealNext());
    Game restored;
    ASSERT_TRUE(restored.restore(original.state()));
    unchanged(restored, original.state());
    for (size_t i = revealed; i < Game::REELS; ++i) {
      ASSERT_TRUE(original.revealNext());
      ASSERT_TRUE(restored.revealNext());
      unchanged(restored, original.state());
    }
    EXPECT_EQ(restored.state().returnCents, 230000);
    EXPECT_EQ(restored.state().phase, Phase::Settled);
    EXPECT_FALSE(restored.revealNext());
  }
}

TEST(SlotsGame, RejectionSamplingRejectsBiasedValuesForEachIndependentReel) {
  struct Samples {
    std::array<uint32_t, 51> values{};
    size_t position = 0;
  } samples;
  for (size_t reel = 0; reel < Game::REELS; ++reel) {
    for (uint32_t rejected = 0; rejected < 16; ++rejected) samples.values[reel * 17 + rejected] = rejected;
    samples.values[reel * 17 + 16] = 20 + reel * 6;
  }
  auto random = [](void* context) {
    auto& sequence = *static_cast<Samples*>(context);
    EXPECT_LT(sequence.position, sequence.values.size());
    return sequence.values[sequence.position++ % sequence.values.size()];
  };
  static_assert((uint64_t{1} << 32) % 20 == 16);
  Game game;
  ASSERT_TRUE(game.startRound(100, 100, random, &samples));
  EXPECT_EQ(samples.position, 51u);
  EXPECT_EQ(game.state().reels[0], Symbol::Cherry);
  EXPECT_EQ(game.state().reels[1], Symbol::Lemon);
  EXPECT_EQ(game.state().reels[2], Symbol::Bell);
  EXPECT_EQ(game.state().returnCents, 0);
}

TEST(SlotsGame, ResetRestoresDefaultsFromAnyRevealState) {
  for (size_t revealed = 0; revealed <= Game::REELS; ++revealed) {
    Game game;
    ASSERT_TRUE(spin(game, 18, 18, 18));
    for (size_t i = 0; i < revealed; ++i) ASSERT_TRUE(game.revealNext());
    game.reset();
    Game fresh;
    unchanged(game, fresh.state());
    EXPECT_TRUE(Game::validateState(game.state()));
  }
}

TEST(SlotsGame, RejectsMalformedBettingStateAndInvalidPhaseOrSymbols) {
  Game::State bad;
  bad.wagerCents = 100;
  reject(bad);
  bad = Game::State{};
  bad.returnCents = 1;
  reject(bad);
  bad = Game::State{};
  bad.revealedReels = 1;
  reject(bad);
  bad = Game::State{};
  bad.reels[1] = Symbol::Lemon;
  reject(bad);
  for (unsigned phase = 3; phase <= 255; ++phase) {
    bad = Game::State{};
    bad.phase = static_cast<Phase>(phase);
    reject(bad);
  }
  for (unsigned symbol = 5; symbol <= 255; ++symbol) {
    for (size_t reel = 0; reel < Game::REELS; ++reel) {
      bad = Game::State{};
      bad.reels[reel] = static_cast<Symbol>(symbol);
      EXPECT_EQ(Game::returnMultiplier(bad.reels), 0);
      reject(bad);
    }
  }
  EXPECT_EQ(Game::returnMultiplier(nullptr), 0);
}

TEST(SlotsGame, RejectsInvalidWagersAndWrongPayoutInActiveOrSettledSnapshot) {
  Game game;
  ASSERT_TRUE(spin(game, 18, 18, 18));
  for (const Phase phase : {Phase::Revealing, Phase::Settled}) {
    auto good = game.state();
    good.phase = phase;
    good.revealedReels = phase == Phase::Settled ? Game::REELS : 0;
    ASSERT_TRUE(Game::validateState(good));
    for (const int64_t wager : {-100LL, 0LL, 99LL, 101LL, 99999999901LL, INT64_MAX}) {
      auto bad = good;
      bad.wagerCents = wager;
      reject(bad);
    }
    for (const int64_t returned : {-1LL, 0LL, 9999LL, 10001LL, INT64_MAX}) {
      auto bad = good;
      bad.returnCents = returned;
      reject(bad);
    }
    auto bad = good;
    bad.reels[0] = Symbol::Bar;
    reject(bad);
    bad = good;
    bad.reels[0] = static_cast<Symbol>(255);
    reject(bad);
  }
}

TEST(SlotsGame, RejectsRevealCountsInconsistentWithPhase) {
  Game game;
  ASSERT_TRUE(spin(game, 0, 6, 11));
  for (unsigned count = 3; count <= 255; ++count) {
    auto bad = game.state();
    bad.revealedReels = count;
    reject(bad);
  }
  for (unsigned count = 0; count <= 255; ++count) {
    if (count == 3) continue;
    auto bad = game.state();
    bad.phase = Phase::Settled;
    bad.revealedReels = count;
    reject(bad);
  }
}
