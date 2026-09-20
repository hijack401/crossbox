#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <type_traits>
#include <vector>

#include "util/RouletteGame.h"

namespace {
using Game = RouletteGame;
using Type = Game::Type;
using Phase = Game::Phase;
using Bet = Game::Bet;

struct Case {
  Bet bet;
  std::array<uint8_t, 18> pockets{};
  uint8_t count = 0;
};

void addCase(std::vector<Case>& cases, Bet bet, std::initializer_list<uint8_t> pockets) {
  Case entry{bet};
  for (const auto pocket : pockets) entry.pockets[entry.count++] = pocket;
  cases.push_back(entry);
}

std::vector<Case> allCases() {
  std::vector<Case> cases;
  cases.reserve(157);
  for (uint8_t pocket = 0; pocket <= 36; ++pocket) addCase(cases, {Type::Straight, pocket}, {pocket});
  for (uint8_t pocket = 1; pocket <= 3; ++pocket) addCase(cases, {Type::Split, 0, pocket}, {0, pocket});
  for (uint8_t row = 0; row < 12; ++row) {
    const uint8_t first = row * 3 + 1;
    addCase(cases, {Type::Street, first}, {first, static_cast<uint8_t>(first + 1), static_cast<uint8_t>(first + 2)});
    for (uint8_t column = 0; column < 3; ++column) {
      const uint8_t pocket = first + column;
      if (column < 2)
        addCase(cases, {Type::Split, pocket, static_cast<uint8_t>(pocket + 1)},
                {pocket, static_cast<uint8_t>(pocket + 1)});
      if (row < 11)
        addCase(cases, {Type::Split, pocket, static_cast<uint8_t>(pocket + 3)},
                {pocket, static_cast<uint8_t>(pocket + 3)});
      if (row < 11 && column < 2)
        addCase(cases, {Type::Corner, pocket},
                {pocket, static_cast<uint8_t>(pocket + 1), static_cast<uint8_t>(pocket + 3),
                 static_cast<uint8_t>(pocket + 4)});
    }
    if (row < 11) {
      addCase(cases, {Type::SixLine, first},
              {first, static_cast<uint8_t>(first + 1), static_cast<uint8_t>(first + 2), static_cast<uint8_t>(first + 3),
               static_cast<uint8_t>(first + 4), static_cast<uint8_t>(first + 5)});
    }
  }
  addCase(cases, {Type::Trio, 1}, {0, 1, 2});
  addCase(cases, {Type::Trio, 2}, {0, 2, 3});
  addCase(cases, {Type::FirstFour}, {0, 1, 2, 3});
  addCase(cases, {Type::Red}, {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36});
  addCase(cases, {Type::Black}, {2, 4, 6, 8, 10, 11, 13, 15, 17, 20, 22, 24, 26, 28, 29, 31, 33, 35});
  addCase(cases, {Type::Odd}, {1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35});
  addCase(cases, {Type::Even}, {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36});
  addCase(cases, {Type::Low}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18});
  addCase(cases, {Type::High}, {19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36});
  for (uint8_t group = 1; group <= 3; ++group) {
    Case dozen{{Type::Dozen, group}};
    Case column{{Type::Column, group}};
    for (uint8_t offset = 0; offset < 12; ++offset) {
      dozen.pockets[dozen.count++] = (group - 1) * 12 + offset + 1;
      column.pockets[column.count++] = group + offset * 3;
    }
    cases.push_back(dozen);
    cases.push_back(column);
  }
  return cases;
}

bool sameBet(Bet left, Bet right) {
  return left.type == right.type && left.first == right.first && left.second == right.second;
}

uint32_t fixedRandom(void* context) { return *static_cast<uint32_t*>(context); }

bool spin(Game& game, uint8_t result, int64_t available = Game::MAX_BET_CENTS) {
  uint32_t sample = 37 + result;
  return game.startRound(available, fixedRandom, &sample);
}

void unchanged(const Game& game, const Game::State& before) {
  const auto& actual = game.state();
  EXPECT_EQ(actual.phase, before.phase);
  EXPECT_EQ(actual.betCount, before.betCount);
  EXPECT_EQ(actual.wagerCents, before.wagerCents);
  EXPECT_EQ(actual.returnCents, before.returnCents);
  EXPECT_EQ(actual.result, before.result);
  for (size_t i = 0; i < Game::MAX_BETS; ++i) {
    EXPECT_EQ(actual.bets[i].amountCents, before.bets[i].amountCents);
    EXPECT_TRUE(sameBet(actual.bets[i].bet, before.bets[i].bet));
  }
}

void reject(const Game::State& state) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Red}, 100, 1000));
  const auto before = game.state();
  EXPECT_FALSE(Game::validateState(state));
  EXPECT_FALSE(game.restore(state));
  unchanged(game, before);
}
}  // namespace

TEST(RouletteGame, DefaultsUseFixedStorageAndCannotSpinWithoutBets) {
  static_assert(std::is_trivially_copyable_v<Game::State>);
  static_assert(sizeof(Game::Bet) == 3);
  static_assert(sizeof(Game::Entry) == 16);
  static_assert(sizeof(Game::State) == 280);
  static_assert(sizeof(Game) == sizeof(Game::State));
  Game game;
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_EQ(game.state().phase, Phase::Betting);
  EXPECT_EQ(game.state().betCount, 0);
  EXPECT_EQ(game.state().wagerCents, 0);
  EXPECT_EQ(game.state().returnCents, 0);
  EXPECT_FALSE(game.canSpin(1000));
  EXPECT_FALSE(spin(game, 0));
  EXPECT_FALSE(game.removeBet(0));
  EXPECT_FALSE(game.clearBets());
  EXPECT_FALSE(game.reveal());
  EXPECT_FALSE(game.nextRound());
}

TEST(RouletteGame, EveryLegalBetAndPocketHasCorrectCoverageAndPayout) {
  const auto cases = allCases();
  ASSERT_EQ(cases.size(), 157u);
  size_t outcomes = 0;
  for (const auto& entry : cases) {
    ASSERT_TRUE(Game::validBet(entry.bet));
    const auto odds = 36 / entry.count - 1;
    ASSERT_EQ(Game::profitOdds(entry.bet), odds);
    for (uint8_t pocket = 0; pocket <= 36; ++pocket) {
      SCOPED_TRACE(testing::Message() << static_cast<int>(entry.bet.type) << ':' << +entry.bet.first << ':'
                                      << +entry.bet.second << " pocket=" << +pocket);
      const bool covered = std::find(entry.pockets.begin(), entry.pockets.begin() + entry.count, pocket) !=
                           entry.pockets.begin() + entry.count;
      ASSERT_EQ(Game::covers(entry.bet, pocket), covered);
      Game game;
      ASSERT_TRUE(game.addBet(entry.bet, 1000, 1000));
      ASSERT_TRUE(spin(game, pocket, 1000));
      ASSERT_EQ(game.state().phase, Phase::Spinning);
      ASSERT_EQ(game.state().result, pocket);
      ASSERT_EQ(game.state().returnCents, covered ? 1000 * (odds + 1) : 0);
      ASSERT_TRUE(Game::validateState(game.state()));
      ASSERT_TRUE(game.reveal());
      ASSERT_TRUE(Game::validateState(game.state()));
      ++outcomes;
    }
  }
  EXPECT_EQ(outcomes, 5809u);
}

TEST(RouletteGame, EveryParameterCombinationMatchesIndependentTableGeometry) {
  const auto cases = allCases();
  size_t valid = 0;
  for (uint8_t type = 0; type <= static_cast<uint8_t>(Type::Column); ++type) {
    for (uint8_t first = 0; first <= 36; ++first) {
      for (uint8_t second = 0; second <= 36; ++second) {
        const Bet bet{static_cast<Type>(type), first, second};
        const bool expected =
            std::any_of(cases.begin(), cases.end(), [bet](const auto& entry) { return sameBet(entry.bet, bet); });
        ASSERT_EQ(Game::validBet(bet), expected) << +type << ':' << +first << ':' << +second;
        if (expected)
          ++valid;
        else {
          EXPECT_EQ(Game::profitOdds(bet), 0);
          EXPECT_FALSE(Game::covers(bet, 0));
          EXPECT_FALSE(Game::covers(bet, 36));
        }
      }
    }
  }
  EXPECT_EQ(valid, 157u);
  for (unsigned invalid = 37; invalid <= 255; ++invalid) {
    EXPECT_FALSE(Game::isRed(invalid));
    for (const auto& entry : cases) EXPECT_FALSE(Game::covers(entry.bet, invalid));
    EXPECT_FALSE(Game::validBet({Type::Straight, static_cast<uint8_t>(invalid)}));
  }
  for (unsigned invalid = static_cast<unsigned>(Type::Column) + 1; invalid <= 255; ++invalid) {
    EXPECT_FALSE(Game::validBet({static_cast<Type>(invalid)}));
    EXPECT_EQ(Game::profitOdds({static_cast<Type>(invalid)}), 0);
  }
}

TEST(RouletteGame, StandardWheelColorsHaveEighteenRedEighteenBlackAndGreenZero) {
  const std::array<uint8_t, 18> red{1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36};
  unsigned redCount = 0;
  unsigned blackCount = 0;
  for (uint8_t pocket = 0; pocket <= 36; ++pocket) {
    EXPECT_EQ(Game::isRed(pocket), std::find(red.begin(), red.end(), pocket) != red.end());
    redCount += Game::covers({Type::Red}, pocket);
    blackCount += Game::covers({Type::Black}, pocket);
  }
  EXPECT_EQ(redCount, 18);
  EXPECT_EQ(blackCount, 18);
  EXPECT_FALSE(Game::isRed(0));
}

TEST(RouletteGame, ZeroLosesAllOutsideBetsAndPaysAllValidZeroCombinations) {
  Game outside;
  for (const Type type : {Type::Red, Type::Black, Type::Odd, Type::Even, Type::Low, Type::High})
    ASSERT_TRUE(outside.addBet({type}, 100, 1200));
  for (uint8_t group = 1; group <= 3; ++group) {
    ASSERT_TRUE(outside.addBet({Type::Dozen, group}, 100, 1200));
    ASSERT_TRUE(outside.addBet({Type::Column, group}, 100, 1200));
  }
  ASSERT_TRUE(spin(outside, 0, 1200));
  EXPECT_EQ(outside.state().wagerCents, 1200);
  EXPECT_EQ(outside.state().returnCents, 0);

  Game zero;
  ASSERT_TRUE(zero.addBet({Type::Straight, 0}, 100, 700));
  for (uint8_t second = 1; second <= 3; ++second) ASSERT_TRUE(zero.addBet({Type::Split, 0, second}, 100, 700));
  ASSERT_TRUE(zero.addBet({Type::Trio, 1}, 100, 700));
  ASSERT_TRUE(zero.addBet({Type::Trio, 2}, 100, 700));
  ASSERT_TRUE(zero.addBet({Type::FirstFour}, 100, 700));
  ASSERT_TRUE(spin(zero, 0, 700));
  EXPECT_EQ(zero.state().wagerCents, 700);
  EXPECT_EQ(zero.state().returnCents, 12300);
}

TEST(RouletteGame, DuplicateAndReversedSplitBetsMergeIntoOneCanonicalEntry) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Split, 2, 1}, 100, 1000));
  ASSERT_TRUE(game.addBet({Type::Split, 1, 2}, 200, 1000));
  ASSERT_TRUE(game.addBet({Type::Split, 2, 1}, 300, 1000));
  ASSERT_EQ(game.state().betCount, 1);
  EXPECT_EQ(game.state().bets[0].bet.first, 1);
  EXPECT_EQ(game.state().bets[0].bet.second, 2);
  EXPECT_EQ(game.state().bets[0].amountCents, 600);
  EXPECT_EQ(game.state().wagerCents, 600);
  ASSERT_TRUE(game.addBet({Type::Split, 1, 0}, 100, 1000));
  EXPECT_EQ(game.state().bets[1].bet.first, 0);
  EXPECT_EQ(game.state().bets[1].bet.second, 1);
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(RouletteGame, FullBoardRejectsNewBetsButCanIncreaseExistingEntry) {
  Game game;
  for (uint8_t pocket = 0; pocket < Game::MAX_BETS; ++pocket)
    ASSERT_TRUE(game.addBet({Type::Straight, pocket}, 100, 10000));
  const auto before = game.state();
  EXPECT_FALSE(game.canAddBet({Type::Straight, 16}, 100, 10000));
  EXPECT_FALSE(game.addBet({Type::Straight, 16}, 100, 10000));
  unchanged(game, before);
  EXPECT_TRUE(game.canAddBet({Type::Straight, 0}, 100, 10000));
  ASSERT_TRUE(game.addBet({Type::Straight, 0}, 100, 10000));
  EXPECT_EQ(game.state().betCount, 16);
  EXPECT_EQ(game.state().wagerCents, 1700);
  EXPECT_EQ(game.state().bets[0].amountCents, 200);
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(RouletteGame, RejectsInvalidAmountsUnaffordableTotalsAndMalformedBetsWithoutMutation) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Red}, 500, 1000));
  const auto before = game.state();
  for (const int64_t amount : {-100LL, 0LL, 1LL, 99LL, 101LL, 600LL, 99999999901LL, INT64_MAX}) {
    EXPECT_FALSE(game.canAddBet({Type::Black}, amount, 1000));
    EXPECT_FALSE(game.addBet({Type::Black}, amount, 1000));
    unchanged(game, before);
  }
  for (const int64_t available : {-1LL, 0LL, 599LL, 99999999901LL, INT64_MAX}) {
    EXPECT_FALSE(game.addBet({Type::Red}, 100, available));
    unchanged(game, before);
  }
  for (const Bet bet : {Bet{Type::Split, 3, 4},
                        {Type::Split, 0, 4},
                        {Type::Corner, 3},
                        {Type::Street, 0},
                        {Type::SixLine, 34},
                        {Type::Trio, 3},
                        {Type::FirstFour, 1},
                        {Type::Red, 1},
                        {Type::Dozen, 4},
                        {Type::Column, 0},
                        {Type::Straight, 0, 1},
                        {static_cast<Type>(255)}}) {
    EXPECT_FALSE(game.addBet(bet, 100, 1000));
    unchanged(game, before);
  }
  EXPECT_TRUE(game.canAddBet({Type::Red}, 100, 699));
}

TEST(RouletteGame, MaximumStakeReturnAndCombinedTotalsRemainExact) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Straight, 36}, Game::MAX_BET_CENTS - 100, Game::MAX_BET_CENTS));
  EXPECT_FALSE(game.addBet({Type::Red}, 200, Game::MAX_BET_CENTS));
  ASSERT_TRUE(game.addBet({Type::Red}, 100, Game::MAX_BET_CENTS));
  EXPECT_EQ(game.state().wagerCents, Game::MAX_BET_CENTS);
  ASSERT_TRUE(spin(game, 36));
  EXPECT_EQ(game.state().returnCents, (Game::MAX_BET_CENTS - 100) * 36 + 200);
  EXPECT_TRUE(Game::validateState(game.state()));
  ASSERT_TRUE(game.reveal());
  ASSERT_TRUE(game.nextRound());
  ASSERT_TRUE(game.addBet({Type::Straight, 0}, Game::MAX_BET_CENTS, Game::MAX_BET_CENTS));
  EXPECT_FALSE(game.addBet({Type::Straight, 0}, 100, Game::MAX_BET_CENTS));
  ASSERT_TRUE(spin(game, 0));
  EXPECT_EQ(game.state().returnCents, 3599999996400LL);
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(RouletteGame, MultipleOverlappingWinnersAndLosersSettleTogether) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Straight, 7}, 100, 10000));
  ASSERT_TRUE(game.addBet({Type::Red}, 200, 10000));
  ASSERT_TRUE(game.addBet({Type::Dozen, 1}, 300, 10000));
  ASSERT_TRUE(game.addBet({Type::Column, 1}, 400, 10000));
  ASSERT_TRUE(game.addBet({Type::Street, 7}, 500, 10000));
  ASSERT_TRUE(game.addBet({Type::Black}, 600, 10000));
  ASSERT_TRUE(spin(game, 7));
  EXPECT_EQ(game.state().wagerCents, 2100);
  EXPECT_EQ(game.state().returnCents, 3600 + 400 + 900 + 1200 + 6000);
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(RouletteGame, RemovingFirstMiddleLastBetsKeepsTotalsAndUnusedSlotsCanonical) {
  Game game;
  for (uint8_t pocket = 0; pocket < 5; ++pocket)
    ASSERT_TRUE(game.addBet({Type::Straight, pocket}, (pocket + 1) * 100, 1500));
  ASSERT_TRUE(game.removeBet(0));
  EXPECT_EQ(game.state().wagerCents, 1400);
  EXPECT_EQ(game.state().bets[0].bet.first, 1);
  ASSERT_TRUE(game.removeBet(1));
  EXPECT_EQ(game.state().wagerCents, 1100);
  EXPECT_EQ(game.state().bets[1].bet.first, 3);
  ASSERT_TRUE(game.removeBet(2));
  EXPECT_EQ(game.state().wagerCents, 600);
  EXPECT_TRUE(Game::validateState(game.state()));
  const auto before = game.state();
  EXPECT_FALSE(game.removeBet(2));
  EXPECT_FALSE(game.removeBet(SIZE_MAX));
  unchanged(game, before);
  ASSERT_TRUE(game.clearBets());
  EXPECT_EQ(game.state().betCount, 0);
  EXPECT_EQ(game.state().wagerCents, 0);
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_FALSE(game.clearBets());
}

TEST(RouletteGame, SpinningLocksBoardUntilExactlyOneReveal) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Red}, 100, 1000));
  const auto draft = game.state();
  EXPECT_FALSE(game.startRound(1000, nullptr));
  EXPECT_FALSE(spin(game, 1, 99));
  unchanged(game, draft);
  ASSERT_TRUE(spin(game, 1, 1000));
  const auto spinning = game.state();
  EXPECT_FALSE(game.canSpin(1000));
  EXPECT_FALSE(spin(game, 2, 1000));
  EXPECT_FALSE(game.canAddBet({Type::Red}, 100, 1000));
  EXPECT_FALSE(game.addBet({Type::Red}, 100, 1000));
  EXPECT_FALSE(game.removeBet(0));
  EXPECT_FALSE(game.clearBets());
  EXPECT_FALSE(game.nextRound());
  EXPECT_FALSE(game.nextRound(true));
  unchanged(game, spinning);
  ASSERT_TRUE(game.reveal());
  EXPECT_EQ(game.state().phase, Phase::Settled);
  EXPECT_EQ(game.state().returnCents, 200);
  const auto settled = game.state();
  EXPECT_FALSE(game.reveal());
  EXPECT_FALSE(spin(game, 2, 1000));
  EXPECT_FALSE(game.addBet({Type::Red}, 100, 1000));
  EXPECT_FALSE(game.clearBets());
  unchanged(game, settled);
}

TEST(RouletteGame, RebetPreservesDraftButRechecksAffordabilityAfterWalletChanges) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Red}, 700, 1000));
  ASSERT_TRUE(game.addBet({Type::Odd}, 300, 1000));
  ASSERT_TRUE(spin(game, 2, 1000));
  ASSERT_TRUE(game.reveal());
  ASSERT_TRUE(game.nextRound(true));
  EXPECT_EQ(game.state().phase, Phase::Betting);
  EXPECT_EQ(game.state().result, 0);
  EXPECT_EQ(game.state().returnCents, 0);
  EXPECT_EQ(game.state().wagerCents, 1000);
  EXPECT_EQ(game.state().betCount, 2);
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_FALSE(game.canSpin(999));
  EXPECT_FALSE(spin(game, 1, 999));
  EXPECT_TRUE(game.canSpin(1000));
  ASSERT_TRUE(game.removeBet(1));
  EXPECT_TRUE(game.canSpin(700));
  ASSERT_TRUE(spin(game, 1, 700));
  ASSERT_TRUE(game.reveal());
  ASSERT_TRUE(game.nextRound(false));
  EXPECT_EQ(game.state().wagerCents, 0);
  EXPECT_EQ(game.state().betCount, 0);
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(RouletteGame, RestartPreservesDraftLockedOutcomeAndSettledRevealGuard) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Straight, 17}, 100, 100));
  Game restored;
  ASSERT_TRUE(restored.restore(game.state()));
  unchanged(restored, game.state());
  ASSERT_TRUE(spin(game, 17, 100));
  ASSERT_TRUE(restored.restore(game.state()));
  unchanged(restored, game.state());
  EXPECT_FALSE(spin(restored, 2, 100));
  ASSERT_TRUE(restored.reveal());
  EXPECT_EQ(restored.state().returnCents, 3600);
  ASSERT_TRUE(game.restore(restored.state()));
  EXPECT_FALSE(game.reveal());
  unchanged(game, restored.state());
}

TEST(RouletteGame, RejectionSamplingSkipsBiasedTailWithoutChangingDesiredPocket) {
  struct Samples {
    uint32_t calls = 0;
    uint32_t next = 0;
  } samples;
  auto random = [](void* context) {
    auto& values = *static_cast<Samples*>(context);
    ++values.calls;
    return values.next++;
  };
  constexpr auto remainder = (uint64_t{1} << 32) % 37;
  static_assert(remainder > 0);
  Game game;
  ASSERT_TRUE(game.addBet({Type::Straight, static_cast<uint8_t>(remainder)}, 100, 100));
  ASSERT_TRUE(game.startRound(100, random, &samples));
  EXPECT_EQ(samples.calls, remainder + 1);
  EXPECT_EQ(game.state().result, remainder);
  EXPECT_EQ(game.state().returnCents, 3600);
}

TEST(RouletteGame, ResetClearsAllPhasesAndBets) {
  for (const Phase phase : {Phase::Betting, Phase::Spinning, Phase::Settled}) {
    Game game;
    ASSERT_TRUE(game.addBet({Type::Straight, 17}, 100, 100));
    if (phase != Phase::Betting) ASSERT_TRUE(spin(game, 17));
    if (phase == Phase::Settled) ASSERT_TRUE(game.reveal());
    game.reset();
    EXPECT_TRUE(Game::validateState(game.state()));
    EXPECT_EQ(game.state().phase, Phase::Betting);
    EXPECT_EQ(game.state().betCount, 0);
    EXPECT_EQ(game.state().wagerCents, 0);
    EXPECT_EQ(game.state().returnCents, 0);
    EXPECT_EQ(game.state().result, 0);
  }
}

TEST(RouletteGame, RejectsCorruptedDraftTotalsMetadataAndUnusedSlots) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Red}, 100, 100));
  const auto good = game.state();
  auto bad = good;
  bad.betCount = Game::MAX_BETS + 1;
  reject(bad);
  bad = good;
  bad.phase = static_cast<Phase>(3);
  reject(bad);
  bad = good;
  bad.result = 37;
  reject(bad);
  bad = good;
  bad.result = 1;
  reject(bad);
  bad = good;
  bad.returnCents = 1;
  reject(bad);
  bad = good;
  bad.wagerCents += 100;
  reject(bad);
  bad = good;
  bad.wagerCents = -1;
  reject(bad);
  bad = good;
  bad.bets[1].amountCents = 100;
  reject(bad);
  bad = good;
  bad.bets[15].bet.type = Type::Black;
  reject(bad);
  bad = good;
  bad.bets[15].bet.first = 1;
  reject(bad);
  bad = good;
  bad.bets[15].bet.second = 1;
  reject(bad);
}

TEST(RouletteGame, RejectsInvalidEntryAmountsGeometryDuplicatesAndOverflow) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Split, 1, 2}, 100, 100));
  const auto good = game.state();
  for (const int64_t amount : {-100LL, 0LL, 99LL, 101LL, 99999999901LL, INT64_MAX}) {
    auto bad = good;
    bad.bets[0].amountCents = bad.wagerCents = amount;
    reject(bad);
  }
  auto bad = good;
  bad.bets[0].bet = {Type::Split, 2, 1};
  reject(bad);
  bad = good;
  bad.bets[0].bet = {Type::Split, 3, 4};
  reject(bad);
  bad = good;
  bad.bets[1] = bad.bets[0];
  bad.betCount = 2;
  bad.wagerCents = 200;
  reject(bad);
  bad = good;
  bad.bets[0].amountCents = Game::MAX_BET_CENTS;
  bad.bets[1] = {100, {Type::Red}};
  bad.betCount = 2;
  bad.wagerCents = Game::MAX_BET_CENTS + 100;
  reject(bad);
}

TEST(RouletteGame, RejectsLockedRoundsWithoutBetsOrWithWrongPayouts) {
  Game game;
  ASSERT_TRUE(game.addBet({Type::Straight, 17}, 100, 100));
  ASSERT_TRUE(spin(game, 17));
  const auto good = game.state();
  for (const Phase phase : {Phase::Spinning, Phase::Settled}) {
    auto bad = good;
    bad.phase = phase;
    --bad.returnCents;
    reject(bad);
    bad.returnCents = -1;
    reject(bad);
    bad.returnCents = INT64_MAX;
    reject(bad);
    bad = good;
    bad.phase = phase;
    bad.result = 18;
    reject(bad);
    bad = Game::State{};
    bad.phase = phase;
    reject(bad);
  }
}
