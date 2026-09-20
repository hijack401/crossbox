#include "CasinoStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace {
constexpr char FILE_PATH[] = "/.crosspoint/casino.bin";
constexpr char TEMP_PATH[] = "/.crosspoint/casino.tmp";
constexpr char BACKUP_PATH[] = "/.crosspoint/casino.bak";
constexpr uint32_t MAGIC = 0x41435043;  // CPCA, little endian.
constexpr uint16_t VERSION = 1;
constexpr size_t HAND_BYTES = 16 + BlackjackGame::MAX_CARDS + 6;
constexpr uint16_t PAYLOAD_BYTES =
    40 + 4 + BlackjackGame::SHOE_CARDS + 3 + (BlackjackGame::MAX_HANDS + 1) * HAND_BYTES + 3;
constexpr size_t FILE_BYTES = 8 + PAYLOAD_BYTES + 4;

uint32_t updateCrc(uint32_t crc, uint8_t byte) {
  crc ^= byte;
  for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
  return crc;
}

class BinaryWriter {
 public:
  explicit BinaryWriter(HalFile& file) : file(file) {}

  void number(uint64_t value, unsigned bytes, bool checksum = true) {
    for (unsigned i = 0; i < bytes && !failed; ++i) {
      const uint8_t byte = static_cast<uint8_t>(value);
      value >>= 8;
      if (checksum) crc = updateCrc(crc, byte);
      buffer[buffered++] = byte;
      if (buffered == sizeof(buffer)) flush();
    }
  }

  void hand(const BlackjackGame::Hand& hand) {
    number(hand.wagerCents, 8);
    number(hand.returnCents, 8);
    for (const uint8_t card : hand.cards) number(card, 1);
    number(hand.cardCount, 1);
    number(hand.finished, 1);
    number(hand.fromSplit, 1);
    number(hand.splitAces, 1);
    number(hand.doubled, 1);
    number(static_cast<uint8_t>(hand.result), 1);
  }

  bool write(const BlackjackGame::State& state) {
    number(MAGIC, 4);
    number(VERSION, 2);
    number(PAYLOAD_BYTES, 2);
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
    number(crc ^ 0xffffffffU, 4, false);
    flush();
    return !failed;
  }

 private:
  HalFile& file;
  uint8_t buffer[128]{};
  size_t buffered = 0;
  uint32_t crc = 0xffffffffU;
  bool failed = false;

  void flush() {
    if (buffered && !failed && file.write(buffer, buffered) != buffered) failed = true;
    buffered = 0;
  }
};

class BinaryReader {
 public:
  explicit BinaryReader(HalFile& file) : file(file) {}

  uint64_t number(unsigned bytes, bool checksum = true) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes && !failed; ++i) {
      if (offset == buffered) {
        const size_t amount = std::min(remaining, sizeof(buffer));
        if (!amount || file.read(buffer, amount) != static_cast<int>(amount)) {
          failed = true;
          return 0;
        }
        remaining -= amount;
        offset = 0;
        buffered = amount;
      }
      const uint8_t byte = buffer[offset++];
      if (checksum) crc = updateCrc(crc, byte);
      value |= static_cast<uint64_t>(byte) << (i * 8);
    }
    return value;
  }

  int64_t money() {
    const uint64_t value = number(8);
    if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      failed = true;
      return 0;
    }
    return static_cast<int64_t>(value);
  }

  void hand(BlackjackGame::Hand& hand) {
    hand.wagerCents = money();
    hand.returnCents = money();
    for (uint8_t& card : hand.cards) card = number(1);
    hand.cardCount = number(1);
    hand.finished = number(1);
    hand.fromSplit = number(1);
    hand.splitAces = number(1);
    hand.doubled = number(1);
    hand.result = static_cast<BlackjackGame::Result>(number(1));
  }

  bool read(BlackjackGame::State& state) {
    if (number(4) != MAGIC || number(2) != VERSION || number(2) != PAYLOAD_BYTES) return false;
    state.balanceCents = money();
    state.roundWagerCents = money();
    state.roundReturnCents = money();
    state.insuranceCents = money();
    state.insuranceReturnCents = money();
    const uint64_t day = number(4);
    if (day > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) return false;
    state.lastCreditDay = static_cast<int32_t>(day);
    for (uint8_t& card : state.shoe) card = number(1);
    state.shoePosition = number(2);
    state.shoeReady = number(1);
    for (auto& player : state.hands) hand(player);
    hand(state.dealer);
    state.handCount = number(1);
    state.activeHand = number(1);
    state.phase = static_cast<BlackjackGame::Phase>(number(1));
    const uint32_t expected = crc ^ 0xffffffffU;
    return number(4, false) == expected && !failed && !remaining && offset == buffered &&
           BlackjackGame::validateState(state);
  }

 private:
  HalFile& file;
  uint8_t buffer[128]{};
  size_t remaining = FILE_BYTES;
  size_t offset = 0;
  size_t buffered = 0;
  uint32_t crc = 0xffffffffU;
  bool failed = false;
};

bool readState(const char* path, BlackjackGame::State& state) {
  HalFile file;
  if (!Storage.openFileForRead("CAS", path, file) || file.size() != FILE_BYTES) return false;
  BinaryReader reader(file);
  return reader.read(state);
}

bool writeState(const BlackjackGame::State& state) {
  HalFile file;
  if (!Storage.openFileForWrite("CAS", TEMP_PATH, file)) return false;
  BinaryWriter writer(file);
  if (!writer.write(state)) return false;
  file.flush();
  // Close before reading back and renaming the temporary file.
  return file.close();
}

bool sameHand(const BlackjackGame::Hand& left, const BlackjackGame::Hand& right) {
  return left.wagerCents == right.wagerCents && left.returnCents == right.returnCents &&
         std::memcmp(left.cards, right.cards, sizeof(left.cards)) == 0 && left.cardCount == right.cardCount &&
         left.finished == right.finished && left.fromSplit == right.fromSplit && left.splitAces == right.splitAces &&
         left.doubled == right.doubled && left.result == right.result;
}

bool sameState(const BlackjackGame::State& left, const BlackjackGame::State& right) {
  if (left.balanceCents != right.balanceCents || left.roundWagerCents != right.roundWagerCents ||
      left.roundReturnCents != right.roundReturnCents || left.insuranceCents != right.insuranceCents ||
      left.insuranceReturnCents != right.insuranceReturnCents || left.lastCreditDay != right.lastCreditDay ||
      std::memcmp(left.shoe, right.shoe, sizeof(left.shoe)) != 0 || left.shoePosition != right.shoePosition ||
      left.shoeReady != right.shoeReady || left.handCount != right.handCount || left.activeHand != right.activeHand ||
      left.phase != right.phase || !sameHand(left.dealer, right.dealer))
    return false;
  for (size_t i = 0; i < BlackjackGame::MAX_HANDS; ++i) {
    if (!sameHand(left.hands[i], right.hands[i])) return false;
  }
  return true;
}
}  // namespace

bool CasinoStore::load() {
  status = LoadStatus::Error;
  primaryValid = false;
  recoveredFromTemporary = false;
  if (!Storage.ready()) {
    LOG_ERR("CAS", "Storage is unavailable");
    return false;
  }
  if (!Storage.exists(FILE_PATH) && !Storage.exists(BACKUP_PATH) && !Storage.exists(TEMP_PATH)) {
    blackjack.reset();
    status = LoadStatus::Empty;
    return true;
  }

  // Keep the live balance and round intact on failure without putting the shoe on the task stack.
  auto candidate = makeUniqueNoThrow<BlackjackGame::State>();
  if (!candidate) {
    LOG_ERR("CAS", "OOM loading casino state");
    return false;
  }
  static constexpr const char* PATHS[] = {FILE_PATH, BACKUP_PATH, TEMP_PATH};
  for (const char* path : PATHS) {
    if (!Storage.exists(path)) continue;
    if (!readState(path, *candidate) || !blackjack.restore(*candidate)) {
      LOG_ERR("CAS", "Invalid or unreadable casino file: %s", path);
      continue;
    }
    primaryValid = path == FILE_PATH;
    recoveredFromTemporary = path == TEMP_PATH;
    status = primaryValid ? LoadStatus::Loaded : LoadStatus::Recovered;
    LOG_INF("CAS", "Loaded casino state from %s", path);
    return true;
  }
  return false;
}

bool CasinoStore::save() {
  if (isReadOnly() || !Storage.ready() || !BlackjackGame::validateState(blackjack.state())) {
    LOG_ERR("CAS", "Cannot save an unavailable or invalid casino state");
    return false;
  }
  auto verification = makeUniqueNoThrow<BlackjackGame::State>();
  if (!verification) {
    LOG_ERR("CAS", "OOM verifying casino state");
    return false;
  }
  if (recoveredFromTemporary) {
    if ((Storage.exists(BACKUP_PATH) && !Storage.remove(BACKUP_PATH)) || !Storage.rename(TEMP_PATH, BACKUP_PATH)) {
      LOG_ERR("CAS", "Failed to preserve recovered casino state");
      return false;
    }
    recoveredFromTemporary = false;
  }
  if (!Storage.ensureDirectoryExists("/.crosspoint") || !writeState(blackjack.state()) ||
      !readState(TEMP_PATH, *verification) || !sameState(blackjack.state(), *verification)) {
    LOG_ERR("CAS", "Failed to write and verify casino state");
    return false;
  }

  bool movedPrimary = false;
  if (Storage.exists(FILE_PATH)) {
    if (primaryValid) {
      if ((Storage.exists(BACKUP_PATH) && !Storage.remove(BACKUP_PATH)) || !Storage.rename(FILE_PATH, BACKUP_PATH)) {
        LOG_ERR("CAS", "Failed to preserve previous casino state");
        return false;
      }
      movedPrimary = true;
    } else if (!Storage.remove(FILE_PATH)) {
      LOG_ERR("CAS", "Failed to replace invalid casino file");
      return false;
    }
  }
  primaryValid = false;
  if (!Storage.rename(TEMP_PATH, FILE_PATH)) {
    if (movedPrimary) primaryValid = Storage.rename(BACKUP_PATH, FILE_PATH);
    LOG_ERR("CAS", "Failed to install verified casino state");
    return false;
  }
  primaryValid = true;
  status = LoadStatus::Loaded;
  return true;
}
