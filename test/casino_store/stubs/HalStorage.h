#pragma once

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

namespace casinoFake {
inline std::map<std::string, std::string> files;
inline bool ready = true;
inline bool failRead = false;
inline bool failOpenRead = false;
inline bool failOpenWrite = false;
inline bool failClose = false;
inline bool failRemove = false;
inline bool failDirectory = false;
inline bool corruptOnClose = false;
inline int writeRemaining = -1;
inline int readRemaining = -1;
inline int failRenameAt = -1;
inline unsigned writes = 0;

inline void reset() {
  files.clear();
  ready = true;
  failRead = false;
  failOpenRead = false;
  failOpenWrite = false;
  failClose = false;
  failRemove = false;
  failDirectory = false;
  corruptOnClose = false;
  writeRemaining = -1;
  readRemaining = -1;
  failRenameAt = -1;
  writes = 0;
}
}  // namespace casinoFake

class HalFile {
 public:
  std::string path;
  size_t position = 0;
  bool opened = false;

  size_t size() { return casinoFake::files.at(path).size(); }
  int read(void* output, size_t count) {
    if (casinoFake::failRead) return -1;
    const auto& contents = casinoFake::files.at(path);
    size_t amount = std::min(count, contents.size() - position);
    if (casinoFake::readRemaining >= 0) {
      amount = std::min(amount, static_cast<size_t>(casinoFake::readRemaining));
      casinoFake::readRemaining -= static_cast<int>(amount);
    }
    std::memcpy(output, contents.data() + position, amount);
    position += amount;
    return static_cast<int>(amount);
  }
  size_t write(const uint8_t* bytes, size_t count) {
    ++casinoFake::writes;
    const size_t amount =
        casinoFake::writeRemaining < 0 ? count : std::min(count, static_cast<size_t>(casinoFake::writeRemaining));
    casinoFake::files.at(path).append(reinterpret_cast<const char*>(bytes), amount);
    if (casinoFake::writeRemaining >= 0) casinoFake::writeRemaining -= static_cast<int>(amount);
    return amount;
  }
  void flush() {}
  bool close() {
    opened = false;
    if (casinoFake::corruptOnClose && !casinoFake::files.at(path).empty()) casinoFake::files.at(path)[0] ^= 1;
    return !casinoFake::failClose;
  }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  bool ready() { return casinoFake::ready; }
  bool exists(const char* path) { return casinoFake::files.count(path) != 0; }
  bool ensureDirectoryExists(const char*) { return !casinoFake::failDirectory; }
  bool remove(const char* path) { return !casinoFake::failRemove && casinoFake::files.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (casinoFake::failRenameAt == 0) {
      casinoFake::failRenameAt = -1;
      return false;
    }
    if (casinoFake::failRenameAt > 0) --casinoFake::failRenameAt;
    if (!exists(from) || exists(to)) return false;
    casinoFake::files[to] = casinoFake::files[from];
    casinoFake::files.erase(from);
    return true;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    if (casinoFake::failOpenRead || !exists(path)) return false;
    file.path = path;
    file.position = 0;
    file.opened = true;
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    if (casinoFake::failOpenWrite) return false;
    casinoFake::files[path].clear();
    file.path = path;
    file.position = 0;
    file.opened = true;
    return true;
  }
};

#define Storage HalStorage::getInstance()
