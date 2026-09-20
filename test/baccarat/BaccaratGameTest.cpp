#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <type_traits>

#include "util/BaccaratGame.h"

namespace {
using Game = BaccaratGame;
using Bet = Game::Bet;
using Phase = Game::Phase;

uint32_t randomWord(void* context) {
  auto& seed = *static_cast<uint32_t*>(context);
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

uint8_t cardWithValue(const uint8_t value) { return value == 0 ? 9 : value - 1; }

Game prepared(const std::initializer_list<uint8_t> values, const uint16_t start = 0) {
  Game game;
  Game::State state;
  state.shoeReady = 1;
  state.shoePosition = start;
  for (size_t i = 0; i < Game::SHOE_CARDS; ++i) state.shoe[i] = i % 52;
  size_t position = start;
  for (const uint8_t points : values) {
    const uint8_t card = cardWithValue(points);
    auto* found = std::find(state.shoe + position, state.shoe + Game::SHOE_CARDS, card);
    if (found == state.shoe + Game::SHOE_CARDS) {
      found = std::find(state.shoe, state.shoe + start, card);
      EXPECT_NE(found, state.shoe + start);
    }
    std::swap(state.shoe[position++], *found);
  }
  EXPECT_TRUE(game.restore(state));
  return game;
}

Game::Hand hand(const std::initializer_list<uint8_t> values) {
  Game::Hand result;
  for (const uint8_t points : values) result.cards[result.cardCount++] = cardWithValue(points);
  return result;
}

void unchanged(const Game& game, const Game::State& snapshot) {
  EXPECT_EQ(std::memcmp(&game.state(), &snapshot, sizeof(snapshot)), 0);
}

void rejects(const Game::State& state) {
  Game game;
  const auto before = game.state();
  EXPECT_FALSE(Game::validateState(state));
  EXPECT_FALSE(game.restore(state));
  unchanged(game, before);
}

void revealAll(Game& game) {
  const uint8_t total = game.state().player.cardCount + game.state().banker.cardCount;
  for (uint8_t i = game.state().revealedCards; i < total; ++i) ASSERT_TRUE(game.revealNext());
  ASSERT_EQ(game.state().phase, Phase::Settled);
}

void checkRevealSequence(Game& game, const std::initializer_list<uint8_t> visibleMasks) {
  const auto dealt = game.state();
  uint8_t revealed = 0;
  for (const uint8_t expectedMask : visibleMasks) {
    SCOPED_TRACE(testing::Message() << "Revealed cards: " << +revealed);
    if (revealed) ASSERT_TRUE(game.revealNext());
    uint8_t visibleMask = 0;
    for (uint8_t i = 0; i < Game::MAX_CARDS; ++i) {
      if (game.cardRevealed(false, i)) visibleMask |= 1 << (i * 2);
      if (game.cardRevealed(true, i)) visibleMask |= 1 << (i * 2 + 1);
    }
    EXPECT_EQ(visibleMask, expectedMask);
    EXPECT_FALSE(game.cardRevealed(false, 3));
    EXPECT_FALSE(game.cardRevealed(true, 255));
    auto expected = dealt;
    expected.revealedCards = revealed;
    expected.phase = revealed == dealt.player.cardCount + dealt.banker.cardCount ? Phase::Settled : Phase::Revealing;
    unchanged(game, expected);
    EXPECT_TRUE(Game::validateState(game.state()));
    ++revealed;
  }
  ASSERT_EQ(game.state().phase, Phase::Settled);
  const auto settled = game.state();
  EXPECT_FALSE(game.revealNext());
  unchanged(game, settled);
}
}  // namespace

TEST(BaccaratGame, StartsEmptyAndUsesFixedStorage) {
  static_assert(std::is_trivially_copyable_v<Game::State>);
  static_assert(sizeof(Game::State) == 448);
  static_assert(sizeof(Game) == sizeof(Game::State));
  static_assert(static_cast<uint8_t>(Phase::Betting) == 0);
  static_assert(static_cast<uint8_t>(Phase::Settled) == 1);
  static_assert(static_cast<uint8_t>(Phase::Revealing) == 2);
  Game game;
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_EQ(game.state().phase, Phase::Betting);
  EXPECT_EQ(game.state().bet, Bet::Player);
  EXPECT_EQ(game.state().winner, Bet::Tie);
  EXPECT_EQ(game.state().wagerCents, 0);
  EXPECT_EQ(game.state().returnCents, 0);
  EXPECT_EQ(game.state().player.cardCount, 0);
  EXPECT_EQ(game.state().banker.cardCount, 0);
  EXPECT_EQ(game.state().revealedCards, 0);
  EXPECT_FALSE(game.revealNext());
  EXPECT_FALSE(game.cardRevealed(false, 0));
  EXPECT_FALSE(game.cardRevealed(true, 0));
  EXPECT_FALSE(game.nextRound());
}

TEST(BaccaratGame, CardValuesAndTotalsUseOnlyFinalDigit) {
  for (uint8_t suit = 0; suit < 4; ++suit) {
    for (uint8_t rank = 1; rank <= 13; ++rank) {
      EXPECT_EQ(Game::cardValue(suit * 13 + rank - 1), rank < 10 ? rank : 0);
    }
  }
  for (uint8_t first = 0; first <= 9; ++first) {
    for (uint8_t second = 0; second <= 9; ++second) {
      EXPECT_EQ(Game::value(hand({first, second})), (first + second) % 10);
      EXPECT_EQ(Game::natural(hand({first, second})), (first + second) % 10 >= 8);
      for (uint8_t third = 0; third <= 9; ++third) {
        EXPECT_EQ(Game::value(hand({first, second, third})), (first + second + third) % 10);
        EXPECT_FALSE(Game::natural(hand({first, second, third})));
      }
    }
  }
  EXPECT_FALSE(Game::natural(hand({8})));
  EXPECT_EQ(Game::value(hand({})), 0);
}

TEST(BaccaratGame, RejectsInvalidUnaffordableAndFractionalDollarWagersWithoutMutation) {
  Game game;
  const auto before = game.state();
  uint32_t seed = 1;
  for (const int64_t amount : {-1LL, 0LL, 1LL, 99LL, 101LL, 100001LL, 99999999901LL, INT64_MAX}) {
    EXPECT_FALSE(game.canBet(amount, 100000));
    EXPECT_FALSE(game.startRound(Bet::Player, amount, 100000, randomWord, &seed));
    unchanged(game, before);
  }
  for (const int64_t available : {-1LL, 0LL, 99LL, 99999999901LL, INT64_MAX}) {
    EXPECT_FALSE(game.canBet(100, available));
    EXPECT_FALSE(game.startRound(Bet::Player, 100, available, randomWord, &seed));
    unchanged(game, before);
  }
  for (unsigned invalid = 3; invalid < 256; ++invalid) {
    EXPECT_FALSE(game.startRound(static_cast<Bet>(invalid), 100, 100, randomWord, &seed));
    unchanged(game, before);
  }
  EXPECT_TRUE(game.canBet(100, 199));
  EXPECT_TRUE(game.canBet(Game::MAX_BET_CENTS, Game::MAX_BET_CENTS));
  EXPECT_FALSE(game.startRound(Bet::Player, 100, 100, nullptr));
  unchanged(game, before);
}

TEST(BaccaratGame, ExhaustiveDrawingMatrixAndPayoutsMatchIndependentTable) {
  // Columns are the Player's third-card value, from zero through nine.
  static constexpr bool DRAW[8][10] = {
      {true, true, true, true, true, true, true, true, true, true},
      {true, true, true, true, true, true, true, true, true, true},
      {true, true, true, true, true, true, true, true, true, true},
      {true, true, true, true, true, true, true, true, false, true},
      {false, false, true, true, true, true, true, true, false, false},
      {false, false, false, false, true, true, true, true, false, false},
      {false, false, false, false, false, false, true, true, false, false},
      {false, false, false, false, false, false, false, false, false, false},
  };
  size_t checked = 0;
  for (uint8_t player = 0; player <= 9; ++player) {
    for (uint8_t banker = 0; banker <= 9; ++banker) {
      for (uint8_t fifth = 0; fifth <= 9; ++fifth) {
        for (uint8_t sixth = 0; sixth <= 9; ++sixth) {
          const bool natural = player >= 8 || banker >= 8;
          const bool playerDrew = !natural && player <= 5;
          const bool bankerDrew = !natural && (playerDrew ? DRAW[banker][fifth] : banker <= 5);
          const uint8_t playerFinal = (player + (playerDrew ? fifth : 0)) % 10;
          const uint8_t bankerFinal = (banker + (bankerDrew ? (playerDrew ? sixth : fifth) : 0)) % 10;
          const Bet winner = playerFinal == bankerFinal  ? Bet::Tie
                             : playerFinal > bankerFinal ? Bet::Player
                                                         : Bet::Banker;
          for (const Bet bet : {Bet::Player, Bet::Banker, Bet::Tie}) {
            SCOPED_TRACE(testing::Message() << +player << ',' << +banker << ',' << +fifth << ',' << +sixth << ','
                                            << static_cast<int>(bet));
            auto game = prepared({0, 0, player, banker, fifth, sixth});
            ASSERT_TRUE(game.startRound(bet, 100, 100, nullptr));
            const auto& state = game.state();
            ASSERT_EQ(state.phase, Phase::Revealing);
            ASSERT_EQ(state.revealedCards, 0);
            ASSERT_EQ(state.player.cardCount, playerDrew ? 3 : 2);
            ASSERT_EQ(state.banker.cardCount, bankerDrew ? 3 : 2);
            ASSERT_EQ(Game::value(state.player), playerFinal);
            ASSERT_EQ(Game::value(state.banker), bankerFinal);
            ASSERT_EQ(state.shoePosition, 4 + playerDrew + bankerDrew);
            ASSERT_EQ(state.winner, winner);
            const int64_t expected = bet == winner        ? (bet == Bet::Tie      ? 900
                                                             : bet == Bet::Banker ? 195
                                                                                  : 200)
                                     : winner == Bet::Tie ? 100
                                                          : 0;
            ASSERT_EQ(state.returnCents, expected);
            ASSERT_TRUE(Game::validateState(state));
            ++checked;
          }
        }
      }
    }
  }
  EXPECT_EQ(checked, 30000);
}

TEST(BaccaratGame, BankerCommissionIsExactForOneDollarOddAndMaximumWagers) {
  for (const int64_t bet : {100LL, 2300LL, 2500LL, 99999999900LL}) {
    auto game = prepared({8, 9, 0, 0});
    ASSERT_TRUE(game.startRound(Bet::Banker, bet, bet, nullptr));
    EXPECT_EQ(game.state().winner, Bet::Banker);
    EXPECT_EQ(game.state().returnCents, bet + bet * 95 / 100);
    EXPECT_TRUE(Game::validateState(game.state()));
  }
}

TEST(BaccaratGame, MaximumTieReturnDoesNotOverflowOrCapBeforeWalletSettlement) {
  auto game = prepared({9, 9, 0, 0});
  ASSERT_TRUE(game.startRound(Bet::Tie, Game::MAX_BET_CENTS, Game::MAX_BET_CENTS, nullptr));
  EXPECT_EQ(game.state().returnCents, 899999999100LL);
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(BaccaratGame, NaturalRevealsFourCardsInAlternatingOrderBeforeSettlement) {
  auto game = prepared({8, 9, 0, 0});
  ASSERT_TRUE(game.startRound(Bet::Banker, 100, 100, nullptr));
  checkRevealSequence(game, {0, 1, 3, 7, 15});
}

TEST(BaccaratGame, PlayerOnlyThirdCardIsRevealedFifth) {
  auto game = prepared({0, 7, 0, 0, 6});
  ASSERT_TRUE(game.startRound(Bet::Player, 100, 100, nullptr));
  checkRevealSequence(game, {0, 1, 3, 7, 15, 31});
}

TEST(BaccaratGame, BankerOnlyThirdCardIsRevealedFifth) {
  auto game = prepared({6, 0, 0, 0, 7});
  ASSERT_TRUE(game.startRound(Bet::Banker, 100, 100, nullptr));
  checkRevealSequence(game, {0, 1, 3, 7, 15, 47});
}

TEST(BaccaratGame, BothThirdCardsRevealPlayerBeforeBanker) {
  auto game = prepared({0, 0, 0, 0, 6, 7});
  ASSERT_TRUE(game.startRound(Bet::Tie, 100, 100, nullptr));
  checkRevealSequence(game, {0, 1, 3, 7, 15, 31, 63});
}

TEST(BaccaratGame, RevealingRoundRejectsNewWagersAndNextRoundWithoutMutation) {
  auto game = prepared({0, 0, 0, 0, 6, 7});
  ASSERT_TRUE(game.startRound(Bet::Player, 2000, 100000, nullptr));
  for (uint8_t revealed = 0; revealed < 6; ++revealed) {
    const auto before = game.state();
    EXPECT_FALSE(game.canBet(100, 100000));
    EXPECT_FALSE(game.startRound(Bet::Banker, 100, 100000, nullptr));
    EXPECT_FALSE(game.nextRound());
    unchanged(game, before);
    ASSERT_TRUE(game.revealNext());
  }
  EXPECT_EQ(game.state().phase, Phase::Settled);
}

TEST(BaccaratGame, RestoreContinuesEachIntermediateRevealWithoutChangingOutcomeOrShoe) {
  auto game = prepared({0, 0, 0, 0, 6, 7});
  ASSERT_TRUE(game.startRound(Bet::Banker, 4000, 100000, nullptr));
  for (uint8_t revealed = 0; revealed < 6; ++revealed) {
    Game restored;
    ASSERT_TRUE(restored.restore(game.state()));
    unchanged(restored, game.state());
    ASSERT_TRUE(restored.revealNext());
    ASSERT_TRUE(game.revealNext());
    unchanged(restored, game.state());
  }
  Game settled;
  ASSERT_TRUE(settled.restore(game.state()));
  EXPECT_FALSE(settled.revealNext());
  unchanged(settled, game.state());
}

TEST(BaccaratGame, RejectsRevealCountsThatDisagreeWithPhaseOrDealtCards) {
  const auto betting = prepared({}).state();
  for (unsigned revealed = 1; revealed < 256; ++revealed) {
    auto bad = betting;
    bad.revealedCards = revealed;
    rejects(bad);
  }
  auto emptyReveal = betting;
  emptyReveal.phase = Phase::Revealing;
  rejects(emptyReveal);

  auto game = prepared({8, 9, 0, 0});
  ASSERT_TRUE(game.startRound(Bet::Banker, 100, 100, nullptr));
  const auto revealing = game.state();
  for (unsigned revealed = 4; revealed < 256; ++revealed) {
    auto bad = revealing;
    bad.revealedCards = revealed;
    rejects(bad);
  }
  for (unsigned revealed = 0; revealed < 256; ++revealed) {
    if (revealed == 4) continue;
    auto bad = revealing;
    bad.phase = Phase::Settled;
    bad.revealedCards = revealed;
    rejects(bad);
  }
}

TEST(BaccaratGame, CannotRedealSettledRoundAndNextRoundPreservesShoeAndSelection) {
  auto game = prepared({8, 9, 0, 0});
  ASSERT_TRUE(game.startRound(Bet::Banker, 100, 100, nullptr));
  revealAll(game);
  const auto settled = game.state();
  EXPECT_FALSE(game.canBet(100, 100));
  EXPECT_FALSE(game.startRound(Bet::Player, 100, 100, nullptr));
  unchanged(game, settled);
  ASSERT_TRUE(game.nextRound());
  EXPECT_EQ(game.state().bet, Bet::Banker);
  EXPECT_EQ(game.state().winner, Bet::Tie);
  EXPECT_EQ(game.state().phase, Phase::Betting);
  EXPECT_EQ(game.state().wagerCents, 0);
  EXPECT_EQ(game.state().returnCents, 0);
  EXPECT_EQ(game.state().revealedCards, 0);
  EXPECT_EQ(game.state().player.cardCount, 0);
  EXPECT_EQ(game.state().banker.cardCount, 0);
  EXPECT_EQ(game.state().shoePosition, settled.shoePosition);
  EXPECT_EQ(std::memcmp(game.state().shoe, settled.shoe, sizeof(settled.shoe)), 0);
  EXPECT_TRUE(Game::validateState(game.state()));
  const auto betting = game.state();
  EXPECT_FALSE(game.nextRound());
  unchanged(game, betting);
}

TEST(BaccaratGame, RestorePreservesOutcomeAndNextCardsAcrossRestart) {
  auto game = prepared({2, 3, 0, 0, 6, 7});
  ASSERT_TRUE(game.startRound(Bet::Player, 8000, 100000, nullptr));
  revealAll(game);
  Game restored;
  ASSERT_TRUE(restored.restore(game.state()));
  unchanged(restored, game.state());
  ASSERT_TRUE(game.nextRound());
  ASSERT_TRUE(restored.nextRound());
  ASSERT_TRUE(game.startRound(Bet::Tie, 2000, 100000, nullptr));
  ASSERT_TRUE(restored.startRound(Bet::Tie, 2000, 100000, nullptr));
  unchanged(restored, game.state());
}

TEST(BaccaratGame, ExactlySixCardsRemainingCanCompleteRoundWithoutReshuffle) {
  auto game = prepared({0, 0, 0, 0, 0, 0}, Game::SHOE_CARDS - 6);
  ASSERT_TRUE(game.startRound(Bet::Tie, 100, 100, nullptr));
  EXPECT_EQ(game.state().shoePosition, Game::SHOE_CARDS);
  EXPECT_EQ(game.state().player.cardCount, 3);
  EXPECT_EQ(game.state().banker.cardCount, 3);
  EXPECT_TRUE(Game::validateState(game.state()));
}

TEST(BaccaratGame, FewerThanSixCardsRequiresRandomBeforeAnyMutation) {
  for (uint16_t remaining = 0; remaining < 6; ++remaining) {
    auto game = prepared({}, Game::SHOE_CARDS - remaining);
    const auto before = game.state();
    EXPECT_FALSE(game.startRound(Bet::Player, 100, 100, nullptr));
    unchanged(game, before);
    uint32_t seed = 124;
    ASSERT_TRUE(game.startRound(Bet::Player, 100, 100, randomWord, &seed));
    EXPECT_LE(game.state().shoePosition, 6);
    EXPECT_TRUE(Game::validateState(game.state()));
  }
}

TEST(BaccaratGame, ShuffleContainsEightCopiesOfEachCardAndContinuesAcrossManyShoes) {
  Game game;
  uint32_t seed = 89012;
  uint16_t previousPosition = 0;
  unsigned shoes = 0;
  for (unsigned round = 0; round < 2000; ++round) {
    ASSERT_TRUE(game.startRound(static_cast<Bet>(round % 3), 100, 100, randomWord, &seed));
    const auto& state = game.state();
    if (state.shoePosition <= previousPosition || round == 0) {
      ++shoes;
      std::array<unsigned, 52> counts{};
      for (const uint8_t card : state.shoe) {
        ASSERT_LT(card, 52);
        ++counts[card];
      }
      for (const auto count : counts) EXPECT_EQ(count, 8);
    }
    ASSERT_TRUE(Game::validateState(state));
    previousPosition = state.shoePosition;
    revealAll(game);
    ASSERT_TRUE(game.nextRound());
  }
  EXPECT_GT(shoes, 20);
}

TEST(BaccaratGame, ResetDiscardsShoeAndRoundWithoutChangingDefaults) {
  auto game = prepared({9, 9, 0, 0});
  ASSERT_TRUE(game.startRound(Bet::Tie, 100, 100, nullptr));
  ASSERT_TRUE(game.revealNext());
  game.reset();
  EXPECT_TRUE(Game::validateState(game.state()));
  EXPECT_EQ(game.state().bet, Bet::Player);
  EXPECT_EQ(game.state().winner, Bet::Tie);
  EXPECT_EQ(game.state().shoePosition, 0);
  EXPECT_EQ(game.state().shoeReady, 0);
  EXPECT_EQ(game.state().phase, Phase::Betting);
  EXPECT_EQ(game.state().returnCents, 0);
  EXPECT_EQ(game.state().wagerCents, 0);
  EXPECT_EQ(game.state().revealedCards, 0);
  for (const uint8_t card : game.state().shoe) EXPECT_EQ(card, 0);
}

TEST(BaccaratGame, RejectsInvalidMetadataAndMalformedShoe) {
  const auto good = prepared({}).state();
  auto bad = good;
  bad.phase = static_cast<Phase>(3);
  rejects(bad);
  bad = good;
  bad.bet = static_cast<Bet>(3);
  rejects(bad);
  bad = good;
  bad.winner = static_cast<Bet>(3);
  rejects(bad);
  bad = good;
  bad.shoeReady = 2;
  rejects(bad);
  bad = good;
  bad.shoePosition = Game::SHOE_CARDS + 1;
  rejects(bad);
  bad = good;
  bad.shoe[0] = 52;
  rejects(bad);
  bad = good;
  bad.shoe[0] = bad.shoe[1];
  rejects(bad);
  bad = Game::State{};
  bad.shoePosition = 1;
  rejects(bad);
  bad = Game::State{};
  bad.shoe[20] = 1;
  rejects(bad);
}

TEST(BaccaratGame, RejectsRoundDataInBettingPhase) {
  const auto good = prepared({}).state();
  auto bad = good;
  bad.wagerCents = 100;
  rejects(bad);
  bad = good;
  bad.returnCents = 1;
  rejects(bad);
  bad = good;
  bad.winner = Bet::Player;
  rejects(bad);
  bad = good;
  bad.player.cardCount = 1;
  rejects(bad);
  bad = good;
  bad.banker.cardCount = 1;
  rejects(bad);
  bad = good;
  bad.player.cards[2] = 1;
  rejects(bad);
}

TEST(BaccaratGame, RejectsSettledHandsThatDisagreeWithDealtShoeOrPayout) {
  auto game = prepared({8, 9, 0, 0});
  ASSERT_TRUE(game.startRound(Bet::Banker, 100, 100, nullptr));
  revealAll(game);
  const auto good = game.state();
  auto bad = good;
  bad.wagerCents = 101;
  rejects(bad);
  bad = good;
  bad.wagerCents = Game::MAX_BET_CENTS + 100;
  rejects(bad);
  bad = good;
  bad.returnCents += 1;
  rejects(bad);
  bad = good;
  bad.returnCents = -1;
  rejects(bad);
  bad = good;
  bad.winner = Bet::Player;
  rejects(bad);
  bad = good;
  bad.shoePosition = 0;
  rejects(bad);
  bad = good;
  bad.shoePosition += 1;
  rejects(bad);
  bad = good;
  bad.player.cardCount = 4;
  rejects(bad);
  bad = good;
  bad.banker.cardCount = 1;
  rejects(bad);
  bad = good;
  bad.player.cards[0] = 52;
  rejects(bad);
  bad = good;
  bad.player.cards[0] += 13;
  rejects(bad);
  bad = good;
  bad.player.cards[2] = 1;
  rejects(bad);
  bad = good;
  std::swap(bad.shoe[0], bad.shoe[1]);
  rejects(bad);
  bad = good;
  bad.shoeReady = 0;
  rejects(bad);
}

TEST(BaccaratGame, RejectsForgedExtraDrawAfterNaturalEvenWithMatchingShoe) {
  auto game = prepared({8, 9, 0, 0, 0});
  ASSERT_TRUE(game.startRound(Bet::Banker, 100, 100, nullptr));
  auto bad = game.state();
  bad.player.cards[2] = bad.shoe[bad.shoePosition++];
  bad.player.cardCount = 3;
  rejects(bad);
}

TEST(BaccaratGame, RejectsForgedBankerDrawWhenPlayerThirdCardIsEight) {
  auto game = prepared({0, 3, 0, 0, 8, 0});
  ASSERT_TRUE(game.startRound(Bet::Player, 100, 100, nullptr));
  ASSERT_EQ(game.state().banker.cardCount, 2);
  auto bad = game.state();
  bad.banker.cards[2] = bad.shoe[bad.shoePosition++];
  bad.banker.cardCount = 3;
  rejects(bad);
}

TEST(BaccaratGame, RejectsMissingRequiredThirdCardEvenWhenTotalAndPayoutStaySame) {
  auto game = prepared({0, 0, 0, 0, 0, 0});
  ASSERT_TRUE(game.startRound(Bet::Tie, 100, 100, nullptr));
  auto bad = game.state();
  bad.banker.cards[2] = 0;
  bad.banker.cardCount = 2;
  --bad.shoePosition;
  rejects(bad);
}

TEST(BaccaratGame, RejectsRoundStartingWithoutSixCardReserve) {
  auto game = prepared({8, 9, 0, 0}, Game::SHOE_CARDS - 6);
  ASSERT_TRUE(game.startRound(Bet::Banker, 100, 100, nullptr));
  auto bad = game.state();
  std::rotate(bad.shoe, bad.shoe + Game::SHOE_CARDS - 2, bad.shoe + Game::SHOE_CARDS);
  bad.shoePosition += 2;
  rejects(bad);
}
