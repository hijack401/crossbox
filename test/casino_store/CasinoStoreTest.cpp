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

void checkpoint(CasinoStore& store) {
  ASSERT_TRUE(store.save());
  const auto original = casinoFake::files.at(MAIN);
  CasinoStore reloaded;
  ASSERT_TRUE(reloaded.load());
  expectState(reloaded.game().state(), store.game().state());
  ASSERT_TRUE(reloaded.save());
  EXPECT_EQ(casinoFake::files.at(MAIN), original);
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
  EXPECT_EQ(casinoFake::files.at(MAIN).size(), 594u);
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
