#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <type_traits>

#include "util/BlackjackGame.h"

namespace {
using Game = BlackjackGame;
using Phase = Game::Phase;
using Result = Game::Result;

uint32_t randomWord(void* context) {
  auto& seed = *static_cast<uint32_t*>(context);
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

Game prepared(const std::initializer_list<uint8_t> faces, const int64_t balance = Game::STARTING_BALANCE_CENTS) {
  Game game;
  Game::State state;
  state.balanceCents = balance;
  state.shoeReady = 1;
  for (size_t i = 0; i < Game::SHOE_CARDS; ++i) state.shoe[i] = i % 52;
  size_t position = 0;
  for (const uint8_t face : faces) {
    const auto found = std::find(state.shoe + position, state.shoe + Game::SHOE_CARDS, face - 1);
    EXPECT_NE(found, state.shoe + Game::SHOE_CARDS);
    if (found == state.shoe + Game::SHOE_CARDS) break;
    std::swap(state.shoe[position++], *found);
  }
  EXPECT_TRUE(game.restore(state));
  return game;
}

Game::Hand hand(const std::initializer_list<uint8_t> faces) {
  Game::Hand result;
  for (const uint8_t face : faces) result.cards[result.cardCount++] = face - 1;
  return result;
}

void valid(const Game& game) { EXPECT_TRUE(Game::validateState(game.state())); }
}  // namespace

TEST(BlackjackGame, StartsWithOneThousandDollarsAndNoAllocationsInState) {
  static_assert(std::is_trivially_copyable_v<Game::State>);
  static_assert(sizeof(Game::State) < 1024);
  Game game;
  EXPECT_EQ(game.state().balanceCents, 100000);
  EXPECT_EQ(game.state().phase, Phase::Betting);
  EXPECT_EQ(game.state().lastCreditDay, 0);
  valid(game);
  EXPECT_FALSE(game.hit());
  EXPECT_FALSE(game.stand());
  EXPECT_FALSE(game.split());
  EXPECT_FALSE(game.doubleDown());
  EXPECT_FALSE(game.surrender());
  EXPECT_FALSE(game.insurance(false));
  EXPECT_FALSE(game.nextRound());
}

TEST(BlackjackGame, AcesSwitchBetweenOneAndEleven) {
  EXPECT_EQ(Game::value(hand({1, 1, 9})), 21);
  EXPECT_TRUE(Game::isSoft(hand({1, 1, 9})));
  EXPECT_EQ(Game::value(hand({1, 1, 9, 10})), 21);
  EXPECT_FALSE(Game::isSoft(hand({1, 1, 9, 10})));
  EXPECT_EQ(Game::value(hand({1, 13})), 21);
  EXPECT_TRUE(Game::natural(hand({1, 11})));
  auto split = hand({1, 10});
  split.fromSplit = 1;
  EXPECT_FALSE(Game::natural(split));
  EXPECT_FALSE(Game::natural(hand({7, 7, 7})));
  for (uint8_t suit = 0; suit < 4; ++suit) {
    for (uint8_t rank = 1; rank <= 13; ++rank) {
      const uint8_t card = suit * 13 + rank - 1;
      EXPECT_EQ(Game::rank(card), rank);
      EXPECT_EQ(Game::suit(card), suit);
      EXPECT_EQ(Game::cardValue(card), std::min<uint8_t>(rank, 10));
    }
  }
}

TEST(BlackjackGame, RejectsInvalidUnaffordableAndMissingRandomSourceBets) {
  Game game;
  uint32_t seed = 1;
  for (const int64_t bet : {-100LL, 0LL, 1LL, 99LL, 101LL, 100001LL, INT64_MAX}) {
    EXPECT_FALSE(game.canBet(bet));
    EXPECT_FALSE(game.startRound(bet, randomWord, &seed));
  }
  EXPECT_FALSE(game.startRound(100, nullptr));
  EXPECT_EQ(game.state().balanceCents, 100000);
  EXPECT_TRUE(game.startRound(100, randomWord, &seed));
  EXPECT_FALSE(game.startRound(100, randomWord, &seed));
  valid(game);
  game = prepared({10, 6, 8, 10}, 50);
  EXPECT_FALSE(game.canBet(100));
}

TEST(BlackjackGame, OrdinaryWinLossAndPushReturnCorrectMoney) {
  auto win = prepared({10, 6, 9, 10, 10});
  ASSERT_TRUE(win.startRound(2500, nullptr));
  EXPECT_EQ(win.state().balanceCents, 97500);
  ASSERT_TRUE(win.stand());
  EXPECT_EQ(win.state().hands[0].result, Result::Win);
  EXPECT_EQ(win.state().balanceCents, 102500);
  EXPECT_EQ(win.state().roundReturnCents, 5000);
  valid(win);
  auto loss = prepared({10, 10, 7, 9});
  ASSERT_TRUE(loss.startRound(2500, nullptr));
  ASSERT_TRUE(loss.stand());
  EXPECT_EQ(loss.state().hands[0].result, Result::Lose);
  EXPECT_EQ(loss.state().balanceCents, 97500);
  valid(loss);
  auto push = prepared({10, 10, 8, 8});
  ASSERT_TRUE(push.startRound(2500, nullptr));
  ASSERT_TRUE(push.stand());
  EXPECT_EQ(push.state().hands[0].result, Result::Push);
  EXPECT_EQ(push.state().balanceCents, 100000);
  valid(push);
}

TEST(BlackjackGame, NaturalPaysThreeToTwoIncludingHalfDollars) {
  auto game = prepared({1, 9, 10, 7});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  EXPECT_EQ(game.state().phase, Phase::Settled);
  EXPECT_EQ(game.state().hands[0].result, Result::Blackjack);
  EXPECT_EQ(game.state().balanceCents, 103750);
  EXPECT_EQ(game.state().dealer.cardCount, 2);
  EXPECT_EQ(game.state().roundReturnCents, 6250);
  valid(game);
}

TEST(BlackjackGame, DealerTenPeeksBeforeAnyPlayerAction) {
  auto game = prepared({10, 13, 9, 1});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  EXPECT_EQ(game.state().phase, Phase::Settled);
  EXPECT_EQ(game.state().hands[0].result, Result::Lose);
  EXPECT_FALSE(game.canDouble());
  EXPECT_FALSE(game.canSurrender());
  EXPECT_EQ(game.state().balanceCents, 97500);
  valid(game);
}

TEST(BlackjackGame, InsurancePaysTwoToOneAndResolvesDealerNatural) {
  auto game = prepared({10, 1, 9, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  EXPECT_EQ(game.state().phase, Phase::Insurance);
  EXPECT_TRUE(game.canInsure());
  EXPECT_FALSE(game.canHit());
  EXPECT_FALSE(game.canSurrender());
  valid(game);
  ASSERT_TRUE(game.insurance(true));
  EXPECT_EQ(game.state().phase, Phase::Settled);
  EXPECT_EQ(game.state().insuranceCents, 1250);
  EXPECT_EQ(game.state().insuranceReturnCents, 3750);
  EXPECT_EQ(game.state().roundWagerCents, 3750);
  EXPECT_EQ(game.state().balanceCents, 100000);
  valid(game);
}

TEST(BlackjackGame, InsuranceCanLoseWhileMainBetWins) {
  auto game = prepared({10, 1, 10, 8});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.insurance(true));
  EXPECT_EQ(game.state().phase, Phase::Playing);
  EXPECT_EQ(game.state().balanceCents, 96250);
  valid(game);
  ASSERT_TRUE(game.stand());
  EXPECT_EQ(game.state().hands[0].result, Result::Win);
  EXPECT_EQ(game.state().insuranceReturnCents, 0);
  EXPECT_EQ(game.state().balanceCents, 101250);
  valid(game);
}

TEST(BlackjackGame, InsuranceRequiresFundsButDeclineAlwaysWorks) {
  auto game = prepared({10, 1, 9, 10}, 2500);
  ASSERT_TRUE(game.startRound(2500, nullptr));
  EXPECT_FALSE(game.canInsure());
  EXPECT_FALSE(game.insurance(true));
  EXPECT_EQ(game.state().phase, Phase::Insurance);
  ASSERT_TRUE(game.insurance(false));
  EXPECT_EQ(game.state().balanceCents, 0);
  EXPECT_EQ(game.state().phase, Phase::Settled);
  valid(game);
}

TEST(BlackjackGame, BothNaturalsPushAndInsuranceStillPaysSeparately) {
  auto game = prepared({1, 1, 10, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.insurance(true));
  EXPECT_EQ(game.state().hands[0].result, Result::Push);
  EXPECT_EQ(game.state().balanceCents, 102500);
  valid(game);
  game = prepared({1, 1, 10, 9});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.insurance(false));
  EXPECT_EQ(game.state().hands[0].result, Result::Blackjack);
  EXPECT_EQ(game.state().balanceCents, 103750);
  valid(game);
}

TEST(BlackjackGame, DealerStandsOnSoftSeventeenAndDrawsBelowSeventeen) {
  auto game = prepared({10, 1, 7, 6, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.insurance(false));
  ASSERT_TRUE(game.stand());
  EXPECT_EQ(game.state().dealer.cardCount, 2);
  EXPECT_TRUE(Game::isSoft(game.state().dealer));
  EXPECT_EQ(game.state().hands[0].result, Result::Push);
  valid(game);
  game = prepared({10, 1, 8, 5, 1});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.insurance(false));
  ASSERT_TRUE(game.stand());
  EXPECT_EQ(game.state().dealer.cardCount, 3);
  EXPECT_EQ(Game::value(game.state().dealer), 17);
  EXPECT_EQ(game.state().hands[0].result, Result::Win);
  valid(game);
}

TEST(BlackjackGame, BustLosesImmediatelyWithoutUnnecessaryDealerCards) {
  auto game = prepared({10, 6, 9, 10, 5});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.hit());
  EXPECT_EQ(game.state().phase, Phase::Settled);
  EXPECT_EQ(game.state().hands[0].result, Result::Bust);
  EXPECT_EQ(game.state().balanceCents, 97500);
  EXPECT_EQ(game.state().dealer.cardCount, 2);
  valid(game);
}

TEST(BlackjackGame, HitToTwentyOneFinishesHandWithoutNaturalPayout) {
  auto game = prepared({5, 6, 6, 10, 10, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.hit());
  EXPECT_EQ(game.state().phase, Phase::Settled);
  EXPECT_EQ(game.state().hands[0].result, Result::Win);
  EXPECT_EQ(game.state().balanceCents, 102500);
  valid(game);
}

TEST(BlackjackGame, DoubleAddsExactStakeAndDealsOnlyOneCard) {
  auto game = prepared({5, 6, 6, 10, 10, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.doubleDown());
  EXPECT_EQ(game.state().hands[0].cardCount, 3);
  EXPECT_EQ(game.state().hands[0].doubled, 1);
  EXPECT_EQ(game.state().hands[0].wagerCents, 5000);
  EXPECT_EQ(game.state().roundWagerCents, 5000);
  EXPECT_EQ(game.state().roundReturnCents, 10000);
  EXPECT_EQ(game.state().balanceCents, 105000);
  valid(game);
}

TEST(BlackjackGame, DoubleAndSplitCannotSpendBeyondBalanceOrFollowAHit) {
  auto game = prepared({8, 6, 8, 10, 2}, 4900);
  ASSERT_TRUE(game.startRound(2500, nullptr));
  EXPECT_FALSE(game.canDouble());
  EXPECT_FALSE(game.canSplit());
  EXPECT_FALSE(game.doubleDown());
  EXPECT_FALSE(game.split());
  EXPECT_EQ(game.state().balanceCents, 2400);
  ASSERT_TRUE(game.hit());
  EXPECT_FALSE(game.canDouble());
  EXPECT_FALSE(game.canSplit());
  EXPECT_FALSE(game.canSurrender());
  valid(game);
}

TEST(BlackjackGame, DoubledBustLosesBothStakes) {
  auto game = prepared({10, 6, 9, 10, 5});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.doubleDown());
  EXPECT_EQ(game.state().hands[0].result, Result::Bust);
  EXPECT_EQ(game.state().roundWagerCents, 5000);
  EXPECT_EQ(game.state().roundReturnCents, 0);
  EXPECT_EQ(game.state().balanceCents, 95000);
  valid(game);
}

TEST(BlackjackGame, LateSurrenderReturnsHalfStakeIncludingHalfDollars) {
  auto game = prepared({10, 10, 6, 9});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.surrender());
  EXPECT_EQ(game.state().hands[0].result, Result::Surrender);
  EXPECT_EQ(game.state().balanceCents, 98750);
  EXPECT_EQ(game.state().roundReturnCents, 1250);
  EXPECT_EQ(game.state().dealer.cardCount, 2);
  valid(game);
}

TEST(BlackjackGame, SplitPreservesOrderAndAllowsDoubleAfterSplit) {
  auto game = prepared({8, 6, 8, 10, 10, 2, 10, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.split());
  EXPECT_EQ(game.state().handCount, 2);
  EXPECT_EQ(game.state().activeHand, 0);
  EXPECT_EQ(Game::value(game.state().hands[0]), 18);
  EXPECT_EQ(Game::value(game.state().hands[1]), 10);
  EXPECT_EQ(game.state().balanceCents, 95000);
  EXPECT_FALSE(game.canSurrender());
  valid(game);
  ASSERT_TRUE(game.stand());
  EXPECT_EQ(game.state().activeHand, 1);
  ASSERT_TRUE(game.doubleDown());
  EXPECT_EQ(game.state().phase, Phase::Settled);
  EXPECT_EQ(game.state().roundWagerCents, 7500);
  EXPECT_EQ(game.state().balanceCents, 107500);
  valid(game);
}

TEST(BlackjackGame, SplitsEqualValueFaceCards) {
  auto game = prepared({11, 6, 13, 10, 8, 9});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  EXPECT_TRUE(game.canSplit());
  ASSERT_TRUE(game.split());
  EXPECT_EQ(Game::rank(game.state().hands[0].cards[0]), 11);
  EXPECT_EQ(Game::rank(game.state().hands[1].cards[0]), 13);
  valid(game);
}

TEST(BlackjackGame, InsuranceSplitAndDoubleKeepOriginalInsuranceStake) {
  auto game = prepared({8, 1, 8, 8, 2, 3, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.insurance(true));
  ASSERT_TRUE(game.split());
  valid(game);
  ASSERT_TRUE(game.doubleDown());
  EXPECT_EQ(game.state().activeHand, 1);
  EXPECT_EQ(game.state().hands[0].wagerCents, 5000);
  EXPECT_EQ(game.state().insuranceCents, 1250);
  valid(game);
  ASSERT_TRUE(game.stand());
  EXPECT_EQ(game.state().hands[0].result, Result::Win);
  EXPECT_EQ(game.state().hands[1].result, Result::Lose);
  EXPECT_EQ(game.state().roundWagerCents, 8750);
  EXPECT_EQ(game.state().roundReturnCents, 10000);
  EXPECT_EQ(game.state().balanceCents, 101250);
  valid(game);
}

TEST(BlackjackGame, CanResplitUpToFourHandsAndNoFurther) {
  auto game = prepared({8, 6, 8, 10, 8, 2, 8, 3, 2, 4});
  ASSERT_TRUE(game.startRound(1000, nullptr));
  ASSERT_TRUE(game.split());
  ASSERT_TRUE(game.split());
  ASSERT_TRUE(game.split());
  EXPECT_EQ(game.state().handCount, 4);
  EXPECT_EQ(game.state().roundWagerCents, 4000);
  EXPECT_FALSE(game.canSplit());
  EXPECT_FALSE(game.split());
  EXPECT_EQ(Game::value(game.state().hands[0]), 10);
  EXPECT_EQ(Game::value(game.state().hands[1]), 12);
  EXPECT_EQ(Game::value(game.state().hands[2]), 11);
  EXPECT_EQ(Game::value(game.state().hands[3]), 10);
  valid(game);
  while (game.state().phase == Phase::Playing) {
    ASSERT_TRUE(game.stand());
    valid(game);
  }
}

TEST(BlackjackGame, SplitAcesReceiveOneCardAndTwentyOnePaysEvenMoney) {
  auto game = prepared({1, 6, 1, 10, 10, 1, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.split());
  EXPECT_EQ(game.state().phase, Phase::Settled);
  EXPECT_EQ(game.state().handCount, 2);
  EXPECT_EQ(game.state().hands[0].cardCount, 2);
  EXPECT_EQ(game.state().hands[1].cardCount, 2);
  EXPECT_EQ(game.state().hands[0].result, Result::Win);
  EXPECT_EQ(game.state().hands[0].returnCents, 5000);
  EXPECT_FALSE(Game::natural(game.state().hands[0]));
  EXPECT_FALSE(game.canSplit());
  EXPECT_FALSE(game.canDouble());
  EXPECT_FALSE(game.canHit());
  valid(game);
}

TEST(BlackjackGame, SplitTwentyOneAutoAdvancesAndIsNotABlackjack) {
  auto game = prepared({10, 6, 10, 10, 1, 8, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.split());
  EXPECT_EQ(game.state().activeHand, 1);
  EXPECT_EQ(game.state().hands[0].finished, 1);
  EXPECT_FALSE(Game::natural(game.state().hands[0]));
  valid(game);
  ASSERT_TRUE(game.stand());
  EXPECT_EQ(game.state().hands[0].returnCents, 5000);
  valid(game);
}

TEST(BlackjackGame, SettlementCannotBePaidTwiceAndNextRoundPreservesShoeAndBalance) {
  auto game = prepared({1, 9, 10, 7});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  const auto settled = game.state();
  for (int i = 0; i < 3; ++i) {
    EXPECT_FALSE(game.hit());
    EXPECT_FALSE(game.stand());
    EXPECT_FALSE(game.doubleDown());
    EXPECT_FALSE(game.split());
    EXPECT_FALSE(game.surrender());
    EXPECT_FALSE(game.insurance(true));
    EXPECT_EQ(game.state().balanceCents, settled.balanceCents);
  }
  Game restored;
  ASSERT_TRUE(restored.restore(settled));
  EXPECT_FALSE(restored.stand());
  EXPECT_EQ(restored.state().balanceCents, settled.balanceCents);
  ASSERT_TRUE(restored.nextRound());
  EXPECT_EQ(restored.state().balanceCents, settled.balanceCents);
  EXPECT_EQ(restored.state().shoePosition, settled.shoePosition);
  EXPECT_EQ(restored.state().phase, Phase::Betting);
  EXPECT_EQ(restored.state().handCount, 0);
  EXPECT_FALSE(restored.nextRound());
  valid(restored);
}

TEST(BlackjackGame, DailyCreditAnchorsOnceCatchesUpAndIgnoresClockRollback) {
  Game game;
  EXPECT_FALSE(game.applyDailyCredit(0));
  EXPECT_FALSE(game.applyDailyCredit(-1));
  EXPECT_FALSE(game.applyDailyCredit(Game::MAX_CREDIT_DAY + 1));
  ASSERT_TRUE(game.applyDailyCredit(20000));
  EXPECT_EQ(game.state().balanceCents, 100000);
  EXPECT_FALSE(game.applyDailyCredit(20000));
  EXPECT_FALSE(game.applyDailyCredit(19999));
  ASSERT_TRUE(game.applyDailyCredit(20001));
  EXPECT_EQ(game.state().balanceCents, 110000);
  ASSERT_TRUE(game.applyDailyCredit(20004));
  EXPECT_EQ(game.state().balanceCents, 140000);
  Game restored;
  ASSERT_TRUE(restored.restore(game.state()));
  EXPECT_FALSE(restored.applyDailyCredit(20004));
  EXPECT_FALSE(restored.applyDailyCredit(19000));
  EXPECT_EQ(restored.state().balanceCents, 140000);
  valid(restored);
}

TEST(BlackjackGame, EmptyBalanceRecoversTheNextDayAndCreditsSaturateSafely) {
  auto game = prepared({}, 0);
  ASSERT_TRUE(game.applyDailyCredit(1));
  EXPECT_EQ(game.state().balanceCents, 0);
  ASSERT_TRUE(game.applyDailyCredit(2));
  EXPECT_EQ(game.state().balanceCents, 10000);
  ASSERT_TRUE(game.applyDailyCredit(Game::MAX_CREDIT_DAY));
  EXPECT_LE(game.state().balanceCents, Game::MAX_BALANCE_CENTS);
  auto state = game.state();
  state.balanceCents = Game::MAX_BALANCE_CENTS - 50;
  state.lastCreditDay = 100;
  ASSERT_TRUE(game.restore(state));
  ASSERT_TRUE(game.applyDailyCredit(101));
  EXPECT_EQ(game.state().balanceCents, Game::MAX_BALANCE_CENTS);
  ASSERT_TRUE(game.applyDailyCredit(102));
  EXPECT_FALSE(game.applyDailyCredit(102));
  valid(game);
}

TEST(BlackjackGame, PayoutCannotOverflowBalanceCap) {
  auto game = prepared({1, 9, 10, 7}, Game::MAX_BALANCE_CENTS);
  ASSERT_TRUE(game.startRound(Game::MAX_BALANCE_CENTS, nullptr));
  EXPECT_EQ(game.state().balanceCents, Game::MAX_BALANCE_CENTS);
  EXPECT_EQ(game.state().roundReturnCents, Game::MAX_BALANCE_CENTS * 5 / 2);
  valid(game);
}

TEST(BlackjackGame, ShuffledShoeContainsExactlySixOfEveryCard) {
  Game game;
  uint32_t seed = 123456;
  ASSERT_TRUE(game.startRound(100, randomWord, &seed));
  std::array<int, 52> counts{};
  bool changed = false;
  for (size_t i = 0; i < Game::SHOE_CARDS; ++i) {
    ASSERT_LT(game.state().shoe[i], 52);
    ++counts[game.state().shoe[i]];
    changed = changed || game.state().shoe[i] != i % 52;
  }
  EXPECT_TRUE(changed);
  for (const int count : counts) EXPECT_EQ(count, 6);
  valid(game);
}

TEST(BlackjackGame, ReshufflesOnlyBetweenRoundsWhenReserveIsLow) {
  auto game = prepared({});
  auto state = game.state();
  state.shoePosition = 250;
  ASSERT_TRUE(game.restore(state));
  EXPECT_FALSE(game.startRound(100, nullptr));
  EXPECT_EQ(game.state().balanceCents, 100000);
  uint32_t seed = 123456;
  ASSERT_TRUE(game.startRound(100, randomWord, &seed));
  EXPECT_EQ(game.state().shoePosition, 4);
  const auto shoe = game.state();
  while (game.state().phase != Phase::Settled) {
    if (game.state().phase == Phase::Insurance) {
      ASSERT_TRUE(game.insurance(false));
    } else {
      ASSERT_TRUE(game.hit());
    }
    EXPECT_TRUE(std::equal(game.state().shoe, game.state().shoe + Game::SHOE_CARDS, shoe.shoe));
    valid(game);
  }
}

TEST(BlackjackGame, InvalidRestoreNeverChangesCurrentBalanceOrHand) {
  auto game = prepared({8, 6, 8, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  const auto original = game.state();
  auto state = original;
  state.balanceCents = -1;
  EXPECT_FALSE(game.restore(state));
  state = original;
  state.shoe[0] = 52;
  EXPECT_FALSE(game.restore(state));
  state = original;
  state.shoe[5] = state.shoe[4];
  EXPECT_FALSE(game.restore(state));
  state = original;
  state.hands[0].cardCount = 255;
  EXPECT_FALSE(game.restore(state));
  state = original;
  state.handCount = 5;
  EXPECT_FALSE(game.restore(state));
  state = original;
  state.activeHand = 1;
  EXPECT_FALSE(game.restore(state));
  state = original;
  state.roundWagerCents += 100;
  EXPECT_FALSE(game.restore(state));
  state = original;
  state.hands[0].returnCents = 100;
  EXPECT_FALSE(game.restore(state));
  state = original;
  state.shoePosition = Game::SHOE_CARDS;
  EXPECT_FALSE(game.restore(state));
  state = original;
  state.phase = static_cast<Phase>(255);
  EXPECT_FALSE(game.restore(state));
  EXPECT_EQ(game.state().balanceCents, original.balanceCents);
  EXPECT_EQ(game.state().phase, original.phase);
  EXPECT_EQ(game.state().hands[0].cardCount, original.hands[0].cardCount);
  valid(game);
}

TEST(BlackjackGame, RejectsInventedSettlementAndImpossibleInsurance) {
  auto game = prepared({1, 9, 10, 7});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  auto state = game.state();
  ++state.hands[0].returnCents;
  ++state.roundReturnCents;
  EXPECT_FALSE(Game::validateState(state));
  state = game.state();
  state.hands[0].result = Result::Win;
  EXPECT_FALSE(Game::validateState(state));
  state = game.state();
  state.insuranceCents = 1250;
  state.roundWagerCents += 1250;
  EXPECT_FALSE(Game::validateState(state));
  state = game.state();
  state.insuranceReturnCents = INT64_MAX;
  EXPECT_FALSE(Game::validateState(state));
}

TEST(BlackjackGame, RejectsUnfinishedFutureTwentyOneAndMismatchedSplitStakes) {
  auto game = prepared({10, 6, 10, 10, 8, 1});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  ASSERT_TRUE(game.split());
  ASSERT_EQ(game.state().activeHand, 0);
  ASSERT_EQ(Game::value(game.state().hands[1]), 21);
  ASSERT_EQ(game.state().hands[1].finished, 1);
  auto invalid = game.state();
  invalid.hands[1].finished = 0;
  EXPECT_FALSE(Game::validateState(invalid));
  EXPECT_FALSE(game.restore(invalid));
  invalid = game.state();
  invalid.hands[1].wagerCents += 100;
  invalid.roundWagerCents += 100;
  EXPECT_FALSE(Game::validateState(invalid));
  ASSERT_TRUE(game.stand());
  valid(game);
}

TEST(BlackjackGame, RejectsSettledDealerWhoHasNotFinishedDrawing) {
  auto game = prepared({10, 6, 9, 10, 10});
  ASSERT_TRUE(game.startRound(2500, nullptr));
  auto invalid = game.state();
  invalid.hands[0].finished = 1;
  invalid.hands[0].result = Result::Win;
  invalid.hands[0].returnCents = 5000;
  invalid.roundReturnCents = 5000;
  invalid.balanceCents += 5000;
  invalid.phase = Phase::Settled;
  EXPECT_FALSE(Game::validateState(invalid));
}

TEST(BlackjackGame, DeterministicLongRunMaintainsMoneyAndSerializableState) {
  Game game;
  uint32_t seed = 3456789;
  for (int round = 0; round < 4000; ++round) {
    if (game.state().balanceCents < 1000) ASSERT_TRUE(game.applyDailyCredit(round + 1));
    const int64_t before = game.state().balanceCents;
    ASSERT_TRUE(game.startRound(100, randomWord, &seed));
    valid(game);
    int steps = 0;
    while (game.state().phase != Phase::Settled) {
      ASSERT_LT(++steps, 100);
      if (game.state().phase == Phase::Insurance) {
        ASSERT_TRUE(game.insurance(game.canInsure() && (randomWord(&seed) % 2)));
      } else {
        const uint32_t action = randomWord(&seed) % 7;
        if (action == 0 && game.canSplit())
          ASSERT_TRUE(game.split());
        else if (action == 1 && game.canDouble())
          ASSERT_TRUE(game.doubleDown());
        else if (action == 2 && game.canSurrender())
          ASSERT_TRUE(game.surrender());
        else if (action <= 4 && game.canHit())
          ASSERT_TRUE(game.hit());
        else
          ASSERT_TRUE(game.stand());
      }
      ASSERT_TRUE(Game::validateState(game.state())) << "round " << round << ", step " << steps;
      Game resumed;
      ASSERT_TRUE(resumed.restore(game.state()));
      game = resumed;
    }
    EXPECT_EQ(game.state().balanceCents, before - game.state().roundWagerCents + game.state().roundReturnCents);
    ASSERT_TRUE(game.nextRound());
    valid(game);
  }
}
