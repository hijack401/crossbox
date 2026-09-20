#include <HalStorage.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>

#include "CasinoStore.h"

namespace {
constexpr char MAIN[] = "/.crosspoint/casino.bin";
constexpr char TEMP[] = "/.crosspoint/casino.tmp";
constexpr char BACKUP[] = "/.crosspoint/casino.bak";
constexpr size_t CURRENT_FILE_BYTES = 1236;
constexpr size_t ROULETTE_OFFSET = 1037;
constexpr size_t ROULETTE_TOTAL_OFFSET = ROULETTE_OFFSET + RouletteGame::MAX_BETS * 11;

struct SpinSample {
  uint32_t value;
  unsigned calls = 0;
};

uint32_t fixedSpin(void* context) {
  auto& sample = *static_cast<SpinSample*>(context);
  ++sample.calls;
  return sample.value;
}

uint32_t randomCard(void* context) {
  auto& value = *static_cast<uint32_t*>(context);
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  return value;
}

void expectHand(const BlackjackGame::Hand& actual, const BlackjackGame::Hand& expected) {
  EXPECT_EQ(actual.wagerCents, expected.wagerCents);
  EXPECT_EQ(actual.returnCents, expected.returnCents);
  EXPECT_EQ(actual.cardCount, expected.cardCount);
  EXPECT_EQ(actual.finished, expected.finished);
  EXPECT_EQ(actual.fromSplit, expected.fromSplit);
  EXPECT_EQ(actual.splitAces, expected.splitAces);
  EXPECT_EQ(actual.doubled, expected.doubled);
  EXPECT_EQ(actual.result, expected.result);
  EXPECT_TRUE(std::equal(std::begin(actual.cards), std::end(actual.cards), std::begin(expected.cards)));
}

void expectState(const BlackjackGame::State& actual, const BlackjackGame::State& expected) {
  EXPECT_EQ(actual.balanceCents, expected.balanceCents);
  EXPECT_EQ(actual.roundWagerCents, expected.roundWagerCents);
  EXPECT_EQ(actual.roundReturnCents, expected.roundReturnCents);
  EXPECT_EQ(actual.insuranceCents, expected.insuranceCents);
  EXPECT_EQ(actual.insuranceReturnCents, expected.insuranceReturnCents);
  EXPECT_EQ(actual.lastCreditDay, expected.lastCreditDay);
  EXPECT_EQ(actual.shoePosition, expected.shoePosition);
  EXPECT_EQ(actual.shoeReady, expected.shoeReady);
  EXPECT_EQ(actual.handCount, expected.handCount);
  EXPECT_EQ(actual.activeHand, expected.activeHand);
  EXPECT_EQ(actual.phase, expected.phase);
  EXPECT_TRUE(std::equal(std::begin(actual.shoe), std::end(actual.shoe), std::begin(expected.shoe)));
  for (size_t i = 0; i < BlackjackGame::MAX_HANDS; ++i) expectHand(actual.hands[i], expected.hands[i]);
  expectHand(actual.dealer, expected.dealer);
}

void expectState(const BaccaratGame::State& actual, const BaccaratGame::State& expected) {
  EXPECT_EQ(actual.wagerCents, expected.wagerCents);
  EXPECT_EQ(actual.returnCents, expected.returnCents);
  EXPECT_EQ(actual.shoePosition, expected.shoePosition);
  EXPECT_EQ(actual.shoeReady, expected.shoeReady);
  EXPECT_EQ(actual.bet, expected.bet);
  EXPECT_EQ(actual.winner, expected.winner);
  EXPECT_EQ(actual.phase, expected.phase);
  EXPECT_EQ(actual.revealedCards, expected.revealedCards);
  EXPECT_EQ(actual.player.cardCount, expected.player.cardCount);
  EXPECT_EQ(actual.banker.cardCount, expected.banker.cardCount);
  EXPECT_TRUE(std::equal(std::begin(actual.shoe), std::end(actual.shoe), std::begin(expected.shoe)));
  EXPECT_TRUE(
      std::equal(std::begin(actual.player.cards), std::end(actual.player.cards), std::begin(expected.player.cards)));
  EXPECT_TRUE(
      std::equal(std::begin(actual.banker.cards), std::end(actual.banker.cards), std::begin(expected.banker.cards)));
}

void expectState(const RouletteGame::State& actual, const RouletteGame::State& expected) {
  EXPECT_EQ(actual.wagerCents, expected.wagerCents);
  EXPECT_EQ(actual.returnCents, expected.returnCents);
  EXPECT_EQ(actual.betCount, expected.betCount);
  EXPECT_EQ(actual.result, expected.result);
  EXPECT_EQ(actual.phase, expected.phase);
  for (size_t i = 0; i < RouletteGame::MAX_BETS; ++i) {
    EXPECT_EQ(actual.bets[i].amountCents, expected.bets[i].amountCents);
    EXPECT_EQ(actual.bets[i].bet.type, expected.bets[i].bet.type);
    EXPECT_EQ(actual.bets[i].bet.first, expected.bets[i].bet.first);
    EXPECT_EQ(actual.bets[i].bet.second, expected.bets[i].bet.second);
  }
}

void prepareShoe(BlackjackGame& game, std::initializer_list<uint8_t> prefix) {
  auto state = game.state();
  for (size_t i = 0; i < BlackjackGame::SHOE_CARDS; ++i) state.shoe[i] = static_cast<uint8_t>(i % 52);
  size_t position = 0;
  for (uint8_t card : prefix) {
    const auto end = std::end(state.shoe);
    const auto found = std::find(std::begin(state.shoe) + position, end, card);
    ASSERT_NE(found, end);
    std::swap(state.shoe[position++], *found);
  }
  state.shoePosition = 0;
  state.shoeReady = 1;
  ASSERT_TRUE(game.restore(state));
}

void prepareShoe(BaccaratGame& game, std::initializer_list<uint8_t> prefix) {
  auto state = game.state();
  for (size_t i = 0; i < BaccaratGame::SHOE_CARDS; ++i) state.shoe[i] = static_cast<uint8_t>(i % 52);
  size_t position = 0;
  for (uint8_t card : prefix) {
    const auto end = std::end(state.shoe);
    const auto found = std::find(std::begin(state.shoe) + position, end, card);
    ASSERT_NE(found, end);
    std::swap(state.shoe[position++], *found);
  }
  state.shoePosition = 0;
  state.shoeReady = 1;
  ASSERT_TRUE(game.restore(state));
}

void checkpoint(CasinoStore& store) {
  ASSERT_TRUE(store.save());
  const auto original = casinoFake::files.at(MAIN);
  CasinoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  expectState(reloaded.game().state(), store.game().state());
  expectState(reloaded.baccarat().state(), store.baccarat().state());
  expectState(reloaded.roulette().state(), store.roulette().state());
  ASSERT_TRUE(reloaded.save());
  EXPECT_EQ(casinoFake::files.at(MAIN), original);
}

void finishBaccarat(CasinoStore& store) {
  while (store.baccarat().state().phase == BaccaratGame::Phase::Revealing) ASSERT_TRUE(store.revealBaccarat());
  ASSERT_EQ(store.baccarat().state().phase, BaccaratGame::Phase::Settled);
}

void repairChecksum(std::string& bytes) {
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i + 4 < bytes.size(); ++i) {
    crc ^= static_cast<uint8_t>(bytes[i]);
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
  }
  crc ^= 0xffffffffU;
  for (unsigned i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = static_cast<char>(crc >> (8 * i));
}

std::string legacySnapshot(const BlackjackGame::State& state) {
  std::string bytes;
  bytes.reserve(594);
  const auto number = [&bytes](uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) {
      bytes += static_cast<char>(value);
      value >>= 8;
    }
  };
  const auto hand = [&number](const BlackjackGame::Hand& value) {
    number(value.wagerCents, 8);
    number(value.returnCents, 8);
    for (const uint8_t card : value.cards) number(card, 1);
    number(value.cardCount, 1);
    number(value.finished, 1);
    number(value.fromSplit, 1);
    number(value.splitAces, 1);
    number(value.doubled, 1);
    number(static_cast<uint8_t>(value.result), 1);
  };
  number(0x41435043, 4);
  number(1, 2);
  number(582, 2);
  number(state.balanceCents, 8);
  number(state.roundWagerCents, 8);
  number(state.roundReturnCents, 8);
  number(state.insuranceCents, 8);
  number(state.insuranceReturnCents, 8);
  number(state.lastCreditDay, 4);
  for (const uint8_t card : state.shoe) number(card, 1);
  number(state.shoePosition, 2);
  number(state.shoeReady, 1);
  for (const auto& player : state.hands) hand(player);
  hand(state.dealer);
  number(state.handCount, 1);
  number(state.activeHand, 1);
  number(static_cast<uint8_t>(state.phase), 1);
  number(0, 4);
  repairChecksum(bytes);
  return bytes;
}

std::string versionTwoSnapshot(const BlackjackGame::State& blackjack, const BaccaratGame::State& baccarat) {
  std::string bytes = legacySnapshot(blackjack);
  bytes.resize(bytes.size() - 4);
  bytes.reserve(1040);
  bytes[4] = 2;
  bytes[6] = static_cast<char>(1028 & 0xff);
  bytes[7] = static_cast<char>(1028 >> 8);
  const auto number = [&bytes](uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) {
      bytes += static_cast<char>(value);
      value >>= 8;
    }
  };
  const auto hand = [&number](const BaccaratGame::Hand& value) {
    for (const uint8_t card : value.cards) number(card, 1);
    number(value.cardCount, 1);
  };
  number(baccarat.wagerCents, 8);
  number(baccarat.returnCents, 8);
  for (const uint8_t card : baccarat.shoe) number(card, 1);
  number(baccarat.shoePosition, 2);
  number(baccarat.shoeReady, 1);
  hand(baccarat.player);
  hand(baccarat.banker);
  number(static_cast<uint8_t>(baccarat.bet), 1);
  number(static_cast<uint8_t>(baccarat.winner), 1);
  number(static_cast<uint8_t>(baccarat.phase), 1);
  number(0, 4);
  repairChecksum(bytes);
  return bytes;
}

std::string versionThreeSnapshot(const BlackjackGame::State& blackjack, const BaccaratGame::State& baccarat) {
  std::string bytes = versionTwoSnapshot(blackjack, baccarat);
  bytes[4] = 3;
  bytes[6] = static_cast<char>(1029 & 0xff);
  bytes[7] = static_cast<char>(1029 >> 8);
  bytes.resize(bytes.size() - 4);
  bytes += static_cast<char>(baccarat.revealedCards);
  bytes.resize(bytes.size() + 4);
  repairChecksum(bytes);
  return bytes;
}

class CasinoPersistence : public testing::Test {
 protected:
  void SetUp() override { casinoFake::reset(); }
};
}  // namespace

TEST_F(CasinoPersistence, StartsWithOneThousandDollarsAndPersistsDailyCredit) {
  CasinoStore store;
  EXPECT_TRUE(store.isReadOnly());
  EXPECT_FALSE(store.save());
  ASSERT_TRUE(store.load());
  EXPECT_EQ(store.loadStatus(), CasinoStore::LoadStatus::Empty);
  EXPECT_FALSE(store.isReadOnly());
  EXPECT_EQ(store.game().state().balanceCents, 100000);
  EXPECT_EQ(casinoFake::writes, 0u);
  ASSERT_TRUE(store.game().applyDailyCredit(20000));
  ASSERT_TRUE(store.save());
  EXPECT_EQ(casinoFake::files.at(MAIN).size(), CURRENT_FILE_BYTES);
  EXPECT_EQ(casinoFake::files.at(MAIN).substr(0, 4), "CPCA");
  CasinoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  EXPECT_FALSE(reloaded.game().applyDailyCredit(20000));
  ASSERT_TRUE(reloaded.game().applyDailyCredit(20003));
  EXPECT_EQ(reloaded.game().state().balanceCents, 130000);
  checkpoint(reloaded);
}

TEST_F(CasinoPersistence, ResumesMidHandWithSameCardsBalanceAndFutureShoe) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  prepareShoe(store.game(), {9, 6, 7, 8, 1});
  uint32_t seed = 314159;
  ASSERT_TRUE(store.game().startRound(2500, randomCard, &seed));
  ASSERT_EQ(store.game().state().phase, BlackjackGame::Phase::Playing);
  checkpoint(store);
  ASSERT_TRUE(store.game().hit());
  checkpoint(store);
  CasinoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  ASSERT_TRUE(store.game().stand());
  ASSERT_TRUE(reloaded.game().stand());
  expectState(reloaded.game().state(), store.game().state());
  checkpoint(store);
  ASSERT_TRUE(store.game().nextRound());
  checkpoint(store);
}

TEST_F(CasinoPersistence, ResumesInsuranceAndSplitHandsWithDoubledWagers) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  prepareShoe(store.game(), {7, 0, 7, 8, 1, 2, 3, 4, 5});
  uint32_t seed = 271828;
  ASSERT_TRUE(store.game().startRound(5000, randomCard, &seed));
  ASSERT_EQ(store.game().state().phase, BlackjackGame::Phase::Insurance);
  checkpoint(store);
  ASSERT_TRUE(store.game().insurance(true));
  checkpoint(store);
  ASSERT_TRUE(store.game().split());
  checkpoint(store);
  ASSERT_TRUE(store.game().doubleDown());
  checkpoint(store);
  while (store.game().canStand()) ASSERT_TRUE(store.game().stand());
  EXPECT_EQ(store.game().state().phase, BlackjackGame::Phase::Settled);
  checkpoint(store);
}

TEST_F(CasinoPersistence, EverySingleByteMutationIsRejectedWithoutResettingBalance) {
  CasinoStore initial;
  ASSERT_TRUE(initial.load());
  ASSERT_TRUE(initial.save());
  const auto valid = casinoFake::files.at(MAIN);
  for (size_t byte = 0; byte < valid.size(); ++byte) {
    SCOPED_TRACE(byte);
    casinoFake::files[MAIN] = valid;
    casinoFake::files[MAIN][byte] ^= 0x40;
    CasinoStore store;
    EXPECT_FALSE(store.load());
    EXPECT_TRUE(store.isReadOnly());
    EXPECT_FALSE(store.save());
    EXPECT_EQ(casinoFake::files[MAIN][byte], static_cast<char>(valid[byte] ^ 0x40));
  }
}

TEST_F(CasinoPersistence, RejectsTruncatedOversizedUnsupportedAndSemanticallyInvalidFiles) {
  CasinoStore initial;
  ASSERT_TRUE(initial.load());
  ASSERT_TRUE(initial.save());
  const auto valid = casinoFake::files.at(MAIN);
  CasinoStore store;
  for (size_t size = 0; size < valid.size(); ++size) {
    casinoFake::files[MAIN] = valid.substr(0, size);
    EXPECT_FALSE(store.load()) << size;
  }
  casinoFake::files[MAIN] = valid + 'x';
  EXPECT_FALSE(store.load());
  for (const size_t invalidByte : {size_t{0}, size_t{4}, size_t{6}, valid.size() - 5}) {
    casinoFake::files[MAIN] = valid;
    casinoFake::files[MAIN][invalidByte] = static_cast<char>(0xff);
    repairChecksum(casinoFake::files[MAIN]);
    EXPECT_FALSE(store.load()) << invalidByte;
  }
  casinoFake::files[MAIN] = valid;
  casinoFake::files[MAIN][15] = static_cast<char>(0x80);  // Negative balance encoding.
  repairChecksum(casinoFake::files[MAIN]);
  EXPECT_FALSE(store.load());
}

TEST_F(CasinoPersistence, RecoversBackupWithoutReplacingItWithCorruptPrimary) {
  CasinoStore initial;
  ASSERT_TRUE(initial.load());
  ASSERT_TRUE(initial.game().applyDailyCredit(20000));
  ASSERT_TRUE(initial.save());
  const auto original = casinoFake::files.at(MAIN);
  ASSERT_TRUE(initial.game().applyDailyCredit(20001));
  ASSERT_TRUE(initial.save());
  casinoFake::files[MAIN] = "corrupt";
  CasinoStore recovered;
  ASSERT_TRUE(recovered.load());
  EXPECT_EQ(recovered.loadStatus(), CasinoStore::LoadStatus::Recovered);
  EXPECT_EQ(recovered.game().state().balanceCents, 100000);
  ASSERT_TRUE(recovered.game().applyDailyCredit(20002));
  ASSERT_TRUE(recovered.save());
  EXPECT_EQ(casinoFake::files.at(BACKUP), original);
  CasinoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  EXPECT_EQ(reloaded.game().state().balanceCents, 120000);
}

TEST_F(CasinoPersistence, PreservesOnlyValidTemporaryBeforeAttemptingAnotherSave) {
  CasinoStore initial;
  ASSERT_TRUE(initial.load());
  ASSERT_TRUE(initial.game().applyDailyCredit(20000));
  ASSERT_TRUE(initial.save());
  const auto original = casinoFake::files.at(MAIN);
  casinoFake::files.erase(MAIN);
  casinoFake::files[TEMP] = original;
  casinoFake::files[BACKUP] = "corrupt";
  CasinoStore recovered;
  ASSERT_TRUE(recovered.load());
  ASSERT_EQ(recovered.loadStatus(), CasinoStore::LoadStatus::Recovered);
  ASSERT_TRUE(recovered.game().applyDailyCredit(20001));
  casinoFake::writeRemaining = 20;
  EXPECT_FALSE(recovered.save());
  EXPECT_EQ(casinoFake::files.at(BACKUP), original);
  CasinoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  EXPECT_EQ(reloaded.game().state().balanceCents, 100000);
  casinoFake::writeRemaining = -1;
  ASSERT_TRUE(recovered.save());
  ASSERT_TRUE(reloaded.load());
  EXPECT_EQ(reloaded.game().state().balanceCents, 110000);
}

TEST_F(CasinoPersistence, PrioritizesPrimaryThenBackupThenTemporary) {
  CasinoStore initial;
  ASSERT_TRUE(initial.load());
  ASSERT_TRUE(initial.game().applyDailyCredit(20000));
  ASSERT_TRUE(initial.save());
  const auto first = casinoFake::files.at(MAIN);
  ASSERT_TRUE(initial.game().applyDailyCredit(20001));
  ASSERT_TRUE(initial.save());
  const auto second = casinoFake::files.at(MAIN);
  ASSERT_TRUE(initial.game().applyDailyCredit(20002));
  ASSERT_TRUE(initial.save());
  const auto third = casinoFake::files.at(MAIN);
  casinoFake::files[MAIN] = first;
  casinoFake::files[BACKUP] = second;
  casinoFake::files[TEMP] = third;
  CasinoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  EXPECT_EQ(reloaded.game().state().balanceCents, 100000);
  casinoFake::files.erase(MAIN);
  ASSERT_TRUE(reloaded.load());
  EXPECT_EQ(reloaded.game().state().balanceCents, 110000);
  casinoFake::files.erase(BACKUP);
  ASSERT_TRUE(reloaded.load());
  EXPECT_EQ(reloaded.game().state().balanceCents, 120000);
}

TEST_F(CasinoPersistence, FailedWritesVerificationAndRenamesKeepPreviousDiskAndRetryLiveState) {
  for (unsigned failure = 0; failure < 11; ++failure) {
    SCOPED_TRACE(failure);
    casinoFake::reset();
    CasinoStore store;
    ASSERT_TRUE(store.load());
    ASSERT_TRUE(store.game().applyDailyCredit(20000));
    ASSERT_TRUE(store.save());
    const auto previous = casinoFake::files.at(MAIN);
    casinoFake::files[BACKUP] = previous;
    ASSERT_TRUE(store.game().applyDailyCredit(20001));
    switch (failure) {
      case 0:
        casinoFake::writeRemaining = 20;
        break;
      case 1:
        casinoFake::failRead = true;
        break;
      case 2:
        casinoFake::failClose = true;
        break;
      case 3:
        casinoFake::failDirectory = true;
        break;
      case 4:
        casinoFake::failRenameAt = 0;
        break;
      case 5:
        casinoFake::failRenameAt = 1;
        break;
      case 6:
        casinoFake::failRemove = true;
        break;
      case 7:
        casinoFake::corruptOnClose = true;
        break;
      case 8:
        casinoFake::failOpenWrite = true;
        break;
      case 9:
        casinoFake::failOpenRead = true;
        break;
      case 10:
        casinoFake::readRemaining = 20;
        break;
    }
    EXPECT_FALSE(store.save());
    EXPECT_EQ(casinoFake::files.at(MAIN), previous);
    EXPECT_EQ(store.game().state().balanceCents, 110000);
    casinoFake::writeRemaining = -1;
    casinoFake::readRemaining = -1;
    casinoFake::failRead = false;
    casinoFake::failClose = false;
    casinoFake::failDirectory = false;
    casinoFake::failRemove = false;
    casinoFake::failRenameAt = -1;
    casinoFake::corruptOnClose = false;
    casinoFake::failOpenWrite = false;
    casinoFake::failOpenRead = false;
    CasinoStore restarted;
    ASSERT_TRUE(restarted.load());
    EXPECT_EQ(restarted.game().state().balanceCents, 100000);
    ASSERT_TRUE(store.save());
    ASSERT_TRUE(restarted.load());
    EXPECT_EQ(restarted.game().state().balanceCents, 110000);
  }
}

TEST_F(CasinoPersistence, UnavailableOrUnreadableStorageDoesNotReplaceLiveGame) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  ASSERT_TRUE(store.game().applyDailyCredit(20000));
  ASSERT_TRUE(store.game().applyDailyCredit(20003));
  ASSERT_TRUE(store.save());
  casinoFake::ready = false;
  EXPECT_FALSE(store.load());
  EXPECT_TRUE(store.isReadOnly());
  EXPECT_FALSE(store.save());
  EXPECT_EQ(store.game().state().balanceCents, 130000);
  casinoFake::ready = true;
  casinoFake::failRead = true;
  EXPECT_FALSE(store.load());
  EXPECT_EQ(store.game().state().balanceCents, 130000);
  casinoFake::failRead = false;
  ASSERT_TRUE(store.load());
  EXPECT_EQ(store.game().state().balanceCents, 130000);
}

TEST_F(CasinoPersistence, MigratesLegacyBlackjackHandWithoutChangingWalletDayOrShoe) {
  BlackjackGame legacy;
  ASSERT_TRUE(legacy.applyDailyCredit(20000));
  ASSERT_TRUE(legacy.applyDailyCredit(20002));
  prepareShoe(legacy, {9, 6, 7, 8, 1});
  ASSERT_TRUE(legacy.startRound(2500, nullptr));
  ASSERT_EQ(legacy.state().phase, BlackjackGame::Phase::Playing);
  const auto bytes = legacySnapshot(legacy.state());
  ASSERT_EQ(bytes.size(), 594u);
  casinoFake::files[MAIN] = bytes;

  CasinoStore migrated;
  ASSERT_TRUE(migrated.load());
  expectState(migrated.game().state(), legacy.state());
  EXPECT_EQ(migrated.baccarat().state().phase, BaccaratGame::Phase::Betting);
  EXPECT_EQ(migrated.baccarat().state().shoeReady, 0);
  EXPECT_FALSE(migrated.game().applyDailyCredit(20002));
  EXPECT_EQ(casinoFake::files.at(MAIN), bytes);
  ASSERT_TRUE(migrated.save());
  EXPECT_EQ(casinoFake::files.at(MAIN).size(), CURRENT_FILE_BYTES);
  EXPECT_EQ(static_cast<uint8_t>(casinoFake::files.at(MAIN)[4]), 4);
  EXPECT_EQ(casinoFake::files.at(BACKUP), bytes);
  checkpoint(migrated);
  ASSERT_TRUE(migrated.game().stand());
  ASSERT_TRUE(legacy.stand());
  expectState(migrated.game().state(), legacy.state());
}

TEST_F(CasinoPersistence, SharesWalletAndDailyCreditAcrossBothGamesIncludingBankerCommission) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  ASSERT_TRUE(store.game().applyDailyCredit(20000));
  prepareShoe(store.game(), {9, 6, 7, 8, 1});
  ASSERT_TRUE(store.game().startRound(2500, nullptr));
  EXPECT_EQ(store.game().state().balanceCents, 97500);
  prepareShoe(store.baccarat(), {3, 4, 3, 3});
  ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
  EXPECT_EQ(store.baccarat().state().returnCents, 1950);
  EXPECT_EQ(store.game().state().balanceCents, 96500);
  finishBaccarat(store);
  EXPECT_EQ(store.game().state().balanceCents, 98450);
  EXPECT_EQ(store.game().state().phase, BlackjackGame::Phase::Playing);
  checkpoint(store);
  ASSERT_TRUE(store.game().stand());
  EXPECT_EQ(store.game().state().balanceCents, 100950);
  EXPECT_EQ(store.baccarat().state().returnCents, 1950);
  ASSERT_TRUE(store.game().applyDailyCredit(20001));
  EXPECT_EQ(store.game().state().balanceCents, 110950);
  checkpoint(store);
  CasinoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  EXPECT_FALSE(reloaded.game().applyDailyCredit(20001));
  EXPECT_FALSE(reloaded.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
  EXPECT_EQ(reloaded.game().state().balanceCents, 110950);
}

TEST_F(CasinoPersistence, RestoresSettledBaccaratAndContinuesTheSameShoe) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  uint32_t seed = 12345;
  ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Player, 1000, randomCard, &seed));
  finishBaccarat(store);
  checkpoint(store);
  const auto settled = store.baccarat().state();
  const auto balance = store.game().state().balanceCents;
  CasinoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  ASSERT_TRUE(store.baccarat().nextRound());
  ASSERT_TRUE(reloaded.baccarat().nextRound());
  EXPECT_EQ(reloaded.game().state().balanceCents, balance);
  EXPECT_EQ(reloaded.baccarat().state().shoePosition, settled.shoePosition);
  ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Tie, 2000, nullptr));
  ASSERT_TRUE(reloaded.dealBaccarat(BaccaratGame::Bet::Tie, 2000, nullptr));
  expectState(reloaded.baccarat().state(), store.baccarat().state());
  expectState(reloaded.game().state(), store.game().state());
  checkpoint(reloaded);
}

TEST_F(CasinoPersistence, RejectsBaccaratBeforeLoadingWithoutFundsOrWithInvalidWagers) {
  CasinoStore store;
  uint32_t seed = 12345;
  EXPECT_FALSE(store.dealBaccarat(BaccaratGame::Bet::Player, 1000, randomCard, &seed));
  ASSERT_TRUE(store.load());
  const auto original = store.baccarat().state();
  for (const int64_t wager : {-100LL, 0LL, 99LL, 150LL, 100100LL}) {
    EXPECT_FALSE(store.dealBaccarat(BaccaratGame::Bet::Player, wager, randomCard, &seed));
    expectState(store.baccarat().state(), original);
    EXPECT_EQ(store.game().state().balanceCents, 100000);
  }
  EXPECT_FALSE(store.dealBaccarat(static_cast<BaccaratGame::Bet>(255), 1000, randomCard, &seed));
  EXPECT_FALSE(store.dealBaccarat(BaccaratGame::Bet::Player, 1000, nullptr));
  auto empty = store.game().state();
  empty.balanceCents = 0;
  ASSERT_TRUE(store.game().restore(empty));
  EXPECT_FALSE(store.dealBaccarat(BaccaratGame::Bet::Player, 100, randomCard, &seed));
  EXPECT_EQ(store.game().state().balanceCents, 0);
  expectState(store.baccarat().state(), original);
}

TEST_F(CasinoPersistence, ExternalSettlementChecksInputsAndClampsWalletWithoutChangingBlackjackRound) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  prepareShoe(store.game(), {9, 6, 7, 8, 1});
  ASSERT_TRUE(store.game().startRound(2500, nullptr));
  const auto original = store.game().state();
  EXPECT_FALSE(store.game().settleExternalWager(150, 300));
  EXPECT_FALSE(store.game().settleExternalWager(98000, 0));
  EXPECT_FALSE(store.game().settleExternalWager(100, -1));
  EXPECT_FALSE(store.game().settleExternalWager(100, 901));
  EXPECT_FALSE(store.game().settleExternalWager(BlackjackGame::MAX_BALANCE_CENTS + 100, 0));
  EXPECT_FALSE(store.game().creditExternalReturn(-1));
  EXPECT_FALSE(store.game().creditExternalReturn(BlackjackGame::MAX_BALANCE_CENTS * 36 + 1));
  EXPECT_TRUE(store.game().creditExternalReturn(0));
  expectState(store.game().state(), original);
  auto nearCap = original;
  nearCap.balanceCents = BlackjackGame::MAX_BALANCE_CENTS - 1;
  ASSERT_TRUE(store.game().restore(nearCap));
  prepareShoe(store.baccarat(), {3, 3, 3, 3});
  ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Tie, 100, nullptr));
  finishBaccarat(store);
  EXPECT_EQ(store.game().state().balanceCents, BlackjackGame::MAX_BALANCE_CENTS);
  EXPECT_EQ(store.baccarat().state().returnCents, 900);
  auto expected = original;
  expected.balanceCents = BlackjackGame::MAX_BALANCE_CENTS;
  expectState(store.game().state(), expected);
  checkpoint(store);
}

TEST_F(CasinoPersistence, FailedBaccaratTransactionsKeepCardsAndWalletInTheSameSnapshot) {
  for (unsigned failure = 0; failure < 4; ++failure) {
    SCOPED_TRACE(failure);
    casinoFake::reset();
    CasinoStore store;
    ASSERT_TRUE(store.load());
    prepareShoe(store.baccarat(), {3, 4, 3, 3});
    ASSERT_TRUE(store.save());
    const auto before = store.baccarat().state();
    const auto previous = casinoFake::files.at(MAIN);
    ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
    const auto after = store.baccarat().state();
    switch (failure) {
      case 0:
        casinoFake::writeRemaining = 600;
        break;
      case 1:
        casinoFake::failRenameAt = 1;
        break;
      case 2:
        casinoFake::corruptOnClose = true;
        break;
      case 3:
        casinoFake::readRemaining = 600;
        break;
    }
    EXPECT_FALSE(store.save());
    EXPECT_EQ(casinoFake::files.at(MAIN), previous);
    EXPECT_EQ(store.game().state().balanceCents, 99000);
    expectState(store.baccarat().state(), after);
    casinoFake::writeRemaining = casinoFake::readRemaining = casinoFake::failRenameAt = -1;
    casinoFake::corruptOnClose = false;
    CasinoStore restarted;
    ASSERT_TRUE(restarted.load());
    EXPECT_EQ(restarted.game().state().balanceCents, 100000);
    expectState(restarted.baccarat().state(), before);
    ASSERT_TRUE(store.save());
    ASSERT_TRUE(restarted.load());
    EXPECT_EQ(restarted.game().state().balanceCents, 99000);
    expectState(restarted.baccarat().state(), after);
  }
}

TEST_F(CasinoPersistence, RecoversLegacyBackupWithFreshBaccaratAfterInterruptedMigration) {
  BlackjackGame legacy;
  ASSERT_TRUE(legacy.applyDailyCredit(20000));
  ASSERT_TRUE(legacy.applyDailyCredit(20004));
  casinoFake::files[MAIN] = legacySnapshot(legacy.state());
  CasinoStore migrated;
  ASSERT_TRUE(migrated.load());
  prepareShoe(migrated.baccarat(), {3, 4, 3, 3});
  ASSERT_TRUE(migrated.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
  ASSERT_TRUE(migrated.save());
  casinoFake::files[MAIN] = "corrupt";
  CasinoStore recovered;
  ASSERT_TRUE(recovered.load());
  EXPECT_EQ(recovered.loadStatus(), CasinoStore::LoadStatus::Recovered);
  expectState(recovered.game().state(), legacy.state());
  EXPECT_EQ(recovered.baccarat().state().phase, BaccaratGame::Phase::Betting);
  EXPECT_EQ(recovered.baccarat().state().shoeReady, 0);
  EXPECT_FALSE(recovered.game().applyDailyCredit(20004));
  checkpoint(recovered);
}

TEST_F(CasinoPersistence, RecoversTemporaryWithSettledBaccaratWithoutPayingTwice) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  prepareShoe(store.baccarat(), {3, 4, 3, 3});
  ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
  finishBaccarat(store);
  ASSERT_TRUE(store.save());
  const auto bytes = casinoFake::files.at(MAIN);
  casinoFake::files.erase(MAIN);
  casinoFake::files[TEMP] = bytes;
  CasinoStore recovered;
  ASSERT_TRUE(recovered.load());
  EXPECT_EQ(recovered.loadStatus(), CasinoStore::LoadStatus::Recovered);
  EXPECT_EQ(recovered.game().state().balanceCents, 100950);
  expectState(recovered.baccarat().state(), store.baccarat().state());
  EXPECT_FALSE(recovered.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
  EXPECT_FALSE(recovered.revealBaccarat());
  EXPECT_EQ(recovered.game().state().balanceCents, 100950);
  checkpoint(recovered);
}

TEST_F(CasinoPersistence, RestoresEveryRevealAndCreditsOnlyOnceAfterTheLastCard) {
  CasinoStore store;
  EXPECT_FALSE(store.revealBaccarat());
  ASSERT_TRUE(store.load());
  EXPECT_FALSE(store.revealBaccarat());
  prepareShoe(store.baccarat(), {0, 0, 0, 0, 4, 5});
  ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
  ASSERT_EQ(store.baccarat().state().phase, BaccaratGame::Phase::Revealing);
  ASSERT_EQ(store.baccarat().state().player.cardCount + store.baccarat().state().banker.cardCount, 6);
  EXPECT_EQ(store.baccarat().state().revealedCards, 0);
  EXPECT_EQ(store.game().state().balanceCents, 99000);
  checkpoint(store);
  EXPECT_FALSE(store.baccarat().nextRound());
  EXPECT_FALSE(store.dealBaccarat(BaccaratGame::Bet::Player, 1000, nullptr));

  for (uint8_t count = 1; count <= 6; ++count) {
    CasinoStore resumed;
    ASSERT_TRUE(resumed.load());
    ASSERT_TRUE(resumed.revealBaccarat());
    EXPECT_EQ(resumed.baccarat().state().revealedCards, count);
    EXPECT_EQ(resumed.game().state().balanceCents, count == 6 ? 100950 : 99000);
    EXPECT_EQ(resumed.baccarat().state().phase,
              count == 6 ? BaccaratGame::Phase::Settled : BaccaratGame::Phase::Revealing);
    checkpoint(resumed);
  }
  ASSERT_TRUE(store.load());
  EXPECT_FALSE(store.revealBaccarat());
  EXPECT_EQ(store.game().state().balanceCents, 100950);
  checkpoint(store);
}

TEST_F(CasinoPersistence, PendingBaccaratPreservesAvailableWalletForBlackjackAndDailyCredits) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  ASSERT_TRUE(store.game().applyDailyCredit(20000));
  prepareShoe(store.baccarat(), {3, 4, 3, 3});
  ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
  prepareShoe(store.game(), {9, 6, 7, 8, 1});
  ASSERT_TRUE(store.game().startRound(2500, nullptr));
  EXPECT_EQ(store.game().state().balanceCents, 96500);
  ASSERT_TRUE(store.revealBaccarat());
  checkpoint(store);
  ASSERT_TRUE(store.game().stand());
  EXPECT_EQ(store.game().state().balanceCents, 99000);
  ASSERT_TRUE(store.game().applyDailyCredit(20001));
  EXPECT_EQ(store.game().state().balanceCents, 109000);
  finishBaccarat(store);
  EXPECT_EQ(store.game().state().balanceCents, 110950);
  EXPECT_EQ(store.game().state().phase, BlackjackGame::Phase::Settled);
  checkpoint(store);
}

TEST_F(CasinoPersistence, MigratesVersionTwoSettledAndBettingWithoutPayingAgain) {
  for (const bool settled : {false, true}) {
    SCOPED_TRACE(settled);
    casinoFake::reset();
    BlackjackGame wallet;
    ASSERT_TRUE(wallet.applyDailyCredit(20000));
    BaccaratGame baccarat;
    prepareShoe(baccarat, {3, 4, 3, 3});
    ASSERT_TRUE(baccarat.startRound(BaccaratGame::Bet::Banker, 1000, wallet.state().balanceCents, nullptr));
    while (baccarat.state().phase == BaccaratGame::Phase::Revealing) ASSERT_TRUE(baccarat.revealNext());
    ASSERT_TRUE(wallet.settleExternalWager(1000, baccarat.state().returnCents));
    if (!settled) ASSERT_TRUE(baccarat.nextRound());
    const auto bytes = versionTwoSnapshot(wallet.state(), baccarat.state());
    ASSERT_EQ(bytes.size(), 1040u);
    casinoFake::files[MAIN] = bytes;

    CasinoStore migrated;
    ASSERT_TRUE(migrated.load());
    expectState(migrated.game().state(), wallet.state());
    expectState(migrated.baccarat().state(), baccarat.state());
    EXPECT_FALSE(migrated.revealBaccarat());
    EXPECT_EQ(migrated.game().state().balanceCents, 100950);
    EXPECT_EQ(casinoFake::files.at(MAIN), bytes);
    ASSERT_TRUE(migrated.save());
    EXPECT_EQ(casinoFake::files.at(MAIN).size(), CURRENT_FILE_BYTES);
    EXPECT_EQ(static_cast<uint8_t>(casinoFake::files.at(MAIN)[4]), 4);
    EXPECT_EQ(casinoFake::files.at(BACKUP), bytes);
    checkpoint(migrated);
  }
}

TEST_F(CasinoPersistence, RejectsRevealPhaseInVersionTwoSnapshot) {
  BlackjackGame wallet;
  BaccaratGame baccarat;
  prepareShoe(baccarat, {3, 4, 3, 3});
  ASSERT_TRUE(baccarat.startRound(BaccaratGame::Bet::Banker, 1000, wallet.state().balanceCents, nullptr));
  casinoFake::files[MAIN] = versionTwoSnapshot(wallet.state(), baccarat.state());
  CasinoStore store;
  EXPECT_FALSE(store.load());
  EXPECT_TRUE(store.isReadOnly());
}

TEST_F(CasinoPersistence, FinalRevealSaveFailuresRetryWithoutReplayingPayment) {
  for (unsigned failure = 0; failure < 4; ++failure) {
    SCOPED_TRACE(failure);
    casinoFake::reset();
    CasinoStore store;
    ASSERT_TRUE(store.load());
    prepareShoe(store.baccarat(), {3, 4, 3, 3});
    ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
    for (unsigned reveal = 0; reveal < 3; ++reveal) ASSERT_TRUE(store.revealBaccarat());
    ASSERT_TRUE(store.save());
    const auto previous = casinoFake::files.at(MAIN);
    ASSERT_TRUE(store.revealBaccarat());
    ASSERT_EQ(store.game().state().balanceCents, 100950);
    switch (failure) {
      case 0:
        casinoFake::writeRemaining = 1036;
        break;
      case 1:
        casinoFake::failRenameAt = 1;
        break;
      case 2:
        casinoFake::corruptOnClose = true;
        break;
      case 3:
        casinoFake::readRemaining = 1036;
        break;
    }
    EXPECT_FALSE(store.save());
    EXPECT_EQ(casinoFake::files.at(MAIN), previous);
    EXPECT_FALSE(store.revealBaccarat());
    EXPECT_EQ(store.game().state().balanceCents, 100950);
    casinoFake::writeRemaining = casinoFake::readRemaining = casinoFake::failRenameAt = -1;
    casinoFake::corruptOnClose = false;
    CasinoStore restarted;
    ASSERT_TRUE(restarted.load());
    EXPECT_EQ(restarted.baccarat().state().revealedCards, 3);
    EXPECT_EQ(restarted.game().state().balanceCents, 99000);
    ASSERT_TRUE(restarted.revealBaccarat());
    EXPECT_EQ(restarted.game().state().balanceCents, 100950);
    ASSERT_TRUE(store.save());
    ASSERT_TRUE(restarted.load());
    EXPECT_FALSE(restarted.revealBaccarat());
    EXPECT_EQ(restarted.game().state().balanceCents, 100950);
    expectState(restarted.baccarat().state(), store.baccarat().state());
  }
}

TEST_F(CasinoPersistence, MigratesVersionThreeWithBlackjackAndEveryBaccaratRevealPreserved) {
  for (uint8_t revealed = 0; revealed <= 4; ++revealed) {
    SCOPED_TRACE(revealed);
    casinoFake::reset();
    CasinoStore original;
    ASSERT_TRUE(original.load());
    ASSERT_TRUE(original.game().applyDailyCredit(20000));
    ASSERT_TRUE(original.game().applyDailyCredit(20001));
    prepareShoe(original.game(), {9, 6, 7, 8, 1});
    ASSERT_TRUE(original.game().startRound(2500, nullptr));
    prepareShoe(original.baccarat(), {3, 4, 3, 3});
    ASSERT_TRUE(original.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
    for (uint8_t card = 0; card < revealed; ++card) ASSERT_TRUE(original.revealBaccarat());
    const auto bytes = versionThreeSnapshot(original.game().state(), original.baccarat().state());
    ASSERT_EQ(bytes.size(), 1041u);
    casinoFake::files[MAIN] = bytes;

    CasinoStore migrated;
    ASSERT_TRUE(migrated.load());
    expectState(migrated.game().state(), original.game().state());
    expectState(migrated.baccarat().state(), original.baccarat().state());
    EXPECT_EQ(migrated.roulette().state().phase, RouletteGame::Phase::Betting);
    EXPECT_EQ(migrated.roulette().state().betCount, 0);
    EXPECT_FALSE(migrated.game().applyDailyCredit(20001));
    EXPECT_EQ(casinoFake::files.at(MAIN), bytes);
    ASSERT_TRUE(migrated.save());
    EXPECT_EQ(casinoFake::files.at(MAIN).size(), CURRENT_FILE_BYTES);
    EXPECT_EQ(static_cast<uint8_t>(casinoFake::files.at(MAIN)[4]), 4);
    EXPECT_EQ(casinoFake::files.at(BACKUP), bytes);
    checkpoint(migrated);
    finishBaccarat(migrated);
    EXPECT_EQ(migrated.game().state().balanceCents, 108450);
    ASSERT_TRUE(migrated.game().stand());
    EXPECT_EQ(migrated.game().state().balanceCents, 110950);
    EXPECT_FALSE(migrated.revealBaccarat());
  }
}

TEST_F(CasinoPersistence, ResumesPendingRouletteWithoutResamplingOrDuplicateDebitAndPaysOnce) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  ASSERT_TRUE(store.roulette().addBet({RouletteGame::Type::Straight, 17}, 1000, 100000));
  ASSERT_TRUE(store.roulette().addBet({RouletteGame::Type::Red}, 2000, 100000));
  ASSERT_TRUE(store.roulette().addBet({RouletteGame::Type::Dozen, 2}, 3000, 100000));
  checkpoint(store);
  EXPECT_EQ(store.game().state().balanceCents, 100000);
  SpinSample random{37 + 17};
  ASSERT_TRUE(store.spinRoulette(fixedSpin, &random));
  EXPECT_EQ(random.calls, 1);
  EXPECT_EQ(store.roulette().state().result, 17);
  EXPECT_EQ(store.roulette().state().wagerCents, 6000);
  EXPECT_EQ(store.roulette().state().returnCents, 45000);
  EXPECT_EQ(store.game().state().balanceCents, 94000);
  checkpoint(store);

  CasinoStore resumed;
  ASSERT_TRUE(resumed.load());
  expectState(resumed.roulette().state(), store.roulette().state());
  EXPECT_EQ(resumed.roulette().state().phase, RouletteGame::Phase::Spinning);
  EXPECT_FALSE(resumed.spinRoulette(fixedSpin, &random));
  EXPECT_FALSE(resumed.roulette().nextRound());
  EXPECT_EQ(random.calls, 1);
  EXPECT_EQ(resumed.game().state().balanceCents, 94000);
  ASSERT_TRUE(resumed.revealRoulette());
  EXPECT_EQ(resumed.game().state().balanceCents, 139000);
  EXPECT_FALSE(resumed.revealRoulette());
  checkpoint(resumed);
  ASSERT_TRUE(store.load());
  EXPECT_FALSE(store.revealRoulette());
  EXPECT_EQ(store.game().state().balanceCents, 139000);
  ASSERT_TRUE(store.roulette().nextRound(true));
  EXPECT_EQ(store.roulette().state().betCount, 3);
  EXPECT_EQ(store.game().state().balanceCents, 139000);
  checkpoint(store);
}

TEST_F(CasinoPersistence, SharesWalletAcrossPendingRouletteBlackjackAndBaccarat) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  ASSERT_TRUE(store.game().applyDailyCredit(20000));
  prepareShoe(store.game(), {9, 6, 7, 8, 1});
  ASSERT_TRUE(store.game().startRound(2500, nullptr));
  prepareShoe(store.baccarat(), {3, 4, 3, 3});
  ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
  ASSERT_TRUE(store.roulette().addBet({RouletteGame::Type::Straight, 17}, 1000, 96500));
  SpinSample random{37 + 17};
  ASSERT_TRUE(store.spinRoulette(fixedSpin, &random));
  EXPECT_EQ(store.game().state().balanceCents, 95500);
  checkpoint(store);
  ASSERT_TRUE(store.revealRoulette());
  EXPECT_EQ(store.game().state().balanceCents, 131500);
  EXPECT_EQ(store.baccarat().state().phase, BaccaratGame::Phase::Revealing);
  EXPECT_EQ(store.game().state().phase, BlackjackGame::Phase::Playing);
  finishBaccarat(store);
  EXPECT_EQ(store.game().state().balanceCents, 133450);
  ASSERT_TRUE(store.game().stand());
  EXPECT_EQ(store.game().state().balanceCents, 135950);
  ASSERT_TRUE(store.game().applyDailyCredit(20001));
  EXPECT_EQ(store.game().state().balanceCents, 145950);
  checkpoint(store);
}

TEST_F(CasinoPersistence, RejectsRouletteWhenUnavailableOrDraftExceedsTheAvailableWallet) {
  CasinoStore store;
  SpinSample random{37};
  EXPECT_FALSE(store.spinRoulette(fixedSpin, &random));
  EXPECT_FALSE(store.revealRoulette());
  ASSERT_TRUE(store.load());
  EXPECT_FALSE(store.revealRoulette());
  EXPECT_FALSE(store.spinRoulette(fixedSpin, &random));
  ASSERT_TRUE(store.roulette().addBet({RouletteGame::Type::Straight, 0}, 100000, 100000));
  const auto draft = store.roulette().state();
  EXPECT_FALSE(store.spinRoulette(nullptr));
  ASSERT_TRUE(store.game().settleExternalWager(100000, 0));
  EXPECT_FALSE(store.spinRoulette(fixedSpin, &random));
  EXPECT_EQ(random.calls, 0);
  EXPECT_EQ(store.game().state().balanceCents, 0);
  expectState(store.roulette().state(), draft);
  checkpoint(store);
  ASSERT_TRUE(store.game().applyDailyCredit(20000));
  ASSERT_TRUE(store.game().applyDailyCredit(20010));
  ASSERT_TRUE(store.spinRoulette(fixedSpin, &random));
  EXPECT_EQ(store.game().state().balanceCents, 0);
  EXPECT_EQ(random.calls, 1);
  ASSERT_TRUE(store.revealRoulette());
  EXPECT_EQ(store.game().state().balanceCents, 3600000);
}

TEST_F(CasinoPersistence, AcceptsMaximumStraightPayoutAndClampsOnlyTheWallet) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  auto maximum = store.game().state();
  maximum.balanceCents = BlackjackGame::MAX_BALANCE_CENTS;
  ASSERT_TRUE(store.game().restore(maximum));
  ASSERT_TRUE(store.roulette().addBet({RouletteGame::Type::Straight, 0}, maximum.balanceCents, maximum.balanceCents));
  SpinSample random{37};
  ASSERT_TRUE(store.spinRoulette(fixedSpin, &random));
  EXPECT_EQ(store.game().state().balanceCents, 0);
  EXPECT_EQ(store.roulette().state().returnCents, BlackjackGame::MAX_BALANCE_CENTS * 36);
  checkpoint(store);
  ASSERT_TRUE(store.revealRoulette());
  EXPECT_EQ(store.game().state().balanceCents, BlackjackGame::MAX_BALANCE_CENTS);
  EXPECT_EQ(store.roulette().state().returnCents, BlackjackGame::MAX_BALANCE_CENTS * 36);
  EXPECT_FALSE(store.revealRoulette());
  checkpoint(store);
}

TEST_F(CasinoPersistence, FailedRouletteSpinAndRevealSavesRetryTheSameAtomicState) {
  for (const bool revealing : {false, true}) {
    for (unsigned failure = 0; failure < 4; ++failure) {
      SCOPED_TRACE(revealing);
      SCOPED_TRACE(failure);
      casinoFake::reset();
      CasinoStore store;
      ASSERT_TRUE(store.load());
      ASSERT_TRUE(store.roulette().addBet({RouletteGame::Type::Straight, 17}, 1000, 100000));
      SpinSample random{37 + 17};
      if (revealing) ASSERT_TRUE(store.spinRoulette(fixedSpin, &random));
      ASSERT_TRUE(store.save());
      const auto priorState = store.roulette().state();
      const auto priorBalance = store.game().state().balanceCents;
      const auto previous = casinoFake::files.at(MAIN);
      ASSERT_TRUE(revealing ? store.revealRoulette() : store.spinRoulette(fixedSpin, &random));
      const auto pendingState = store.roulette().state();
      const auto pendingBalance = store.game().state().balanceCents;
      switch (failure) {
        case 0:
          casinoFake::writeRemaining = ROULETTE_OFFSET + 12;
          break;
        case 1:
          casinoFake::failRenameAt = 1;
          break;
        case 2:
          casinoFake::corruptOnClose = true;
          break;
        case 3:
          casinoFake::readRemaining = ROULETTE_OFFSET + 12;
          break;
      }
      EXPECT_FALSE(store.save());
      EXPECT_EQ(casinoFake::files.at(MAIN), previous);
      expectState(store.roulette().state(), pendingState);
      EXPECT_EQ(store.game().state().balanceCents, pendingBalance);
      EXPECT_FALSE(store.spinRoulette(fixedSpin, &random));
      if (revealing) EXPECT_FALSE(store.revealRoulette());
      EXPECT_EQ(random.calls, 1);
      casinoFake::writeRemaining = casinoFake::readRemaining = casinoFake::failRenameAt = -1;
      casinoFake::corruptOnClose = false;
      CasinoStore restarted;
      ASSERT_TRUE(restarted.load());
      expectState(restarted.roulette().state(), priorState);
      EXPECT_EQ(restarted.game().state().balanceCents, priorBalance);
      ASSERT_TRUE(store.save());
      ASSERT_TRUE(restarted.load());
      expectState(restarted.roulette().state(), pendingState);
      EXPECT_EQ(restarted.game().state().balanceCents, pendingBalance);
      if (!revealing) ASSERT_TRUE(restarted.revealRoulette());
      EXPECT_FALSE(restarted.revealRoulette());
      EXPECT_EQ(restarted.game().state().balanceCents, 135000);
    }
  }
}

TEST_F(CasinoPersistence, RecoversPendingRouletteFromBackupOrTemporaryWithoutResampling) {
  for (const bool backup : {false, true}) {
    casinoFake::reset();
    CasinoStore store;
    ASSERT_TRUE(store.load());
    ASSERT_TRUE(store.roulette().addBet({RouletteGame::Type::Straight, 17}, 1000, 100000));
    SpinSample random{37 + 17};
    ASSERT_TRUE(store.spinRoulette(fixedSpin, &random));
    ASSERT_TRUE(store.save());
    const auto pending = casinoFake::files.at(MAIN);
    casinoFake::files[MAIN] = "corrupt";
    casinoFake::files[backup ? BACKUP : TEMP] = pending;
    CasinoStore recovered;
    ASSERT_TRUE(recovered.load());
    EXPECT_EQ(recovered.loadStatus(), CasinoStore::LoadStatus::Recovered);
    expectState(recovered.roulette().state(), store.roulette().state());
    EXPECT_EQ(recovered.game().state().balanceCents, 99000);
    EXPECT_FALSE(recovered.spinRoulette(fixedSpin, &random));
    EXPECT_EQ(random.calls, 1);
    ASSERT_TRUE(recovered.revealRoulette());
    EXPECT_EQ(recovered.game().state().balanceCents, 135000);
    checkpoint(recovered);
    EXPECT_FALSE(recovered.revealRoulette());
  }
}

TEST_F(CasinoPersistence, RejectsMalformedRouletteEvenWithCorrectChecksumWithoutReplacingLiveGames) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  ASSERT_TRUE(store.roulette().addBet({RouletteGame::Type::Straight, 17}, 1000, 100000));
  SpinSample random{37 + 17};
  ASSERT_TRUE(store.spinRoulette(fixedSpin, &random));
  ASSERT_TRUE(store.save());
  const auto valid = casinoFake::files.at(MAIN);
  const auto blackjack = store.game().state();
  const auto roulette = store.roulette().state();
  const size_t badOffsets[] = {ROULETTE_OFFSET + 7,       ROULETTE_OFFSET + 8,        ROULETTE_OFFSET + 9,
                               ROULETTE_OFFSET + 10,      ROULETTE_OFFSET + 11,       ROULETTE_TOTAL_OFFSET,
                               ROULETTE_TOTAL_OFFSET + 8, ROULETTE_TOTAL_OFFSET + 16, ROULETTE_TOTAL_OFFSET + 17,
                               ROULETTE_TOTAL_OFFSET + 18};
  for (const auto offset : badOffsets) {
    SCOPED_TRACE(offset);
    casinoFake::files[MAIN] = valid;
    casinoFake::files[MAIN][offset] = static_cast<char>(0xff);
    repairChecksum(casinoFake::files[MAIN]);
    EXPECT_FALSE(store.load());
    EXPECT_TRUE(store.isReadOnly());
    EXPECT_FALSE(store.revealRoulette());
    expectState(store.game().state(), blackjack);
    expectState(store.roulette().state(), roulette);
  }
}

TEST_F(CasinoPersistence, RecoversLastUnrevealedCardAndWalletTogetherFromBackup) {
  CasinoStore store;
  ASSERT_TRUE(store.load());
  prepareShoe(store.baccarat(), {3, 4, 3, 3});
  ASSERT_TRUE(store.dealBaccarat(BaccaratGame::Bet::Banker, 1000, nullptr));
  for (unsigned reveal = 0; reveal < 3; ++reveal) ASSERT_TRUE(store.revealBaccarat());
  ASSERT_TRUE(store.save());
  ASSERT_TRUE(store.revealBaccarat());
  ASSERT_TRUE(store.save());
  casinoFake::files[MAIN] = "corrupt";
  CasinoStore recovered;
  ASSERT_TRUE(recovered.load());
  EXPECT_EQ(recovered.loadStatus(), CasinoStore::LoadStatus::Recovered);
  EXPECT_EQ(recovered.baccarat().state().revealedCards, 3);
  EXPECT_EQ(recovered.game().state().balanceCents, 99000);
  ASSERT_TRUE(recovered.revealBaccarat());
  EXPECT_FALSE(recovered.revealBaccarat());
  EXPECT_EQ(recovered.game().state().balanceCents, 100950);
  checkpoint(recovered);
}
