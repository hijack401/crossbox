#pragma once

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

namespace todoFake {
inline std::map<std::string, std::string> files;
inline bool ready = true;
inline bool failRead = false;
inline bool failClose = false;
inline bool failRemove = false;
inline bool failDirectory = false;
inline int writeRemaining = -1;
inline int failRenameAt = -1;
inline unsigned writes = 0;

inline void reset() {
  files.clear();
  ready = true;
  failRead = false;
  failClose = false;
  failRemove = false;
  failDirectory = false;
  writeRemaining = -1;
  failRenameAt = -1;
  writes = 0;
}
}  // namespace todoFake

class HalFile {
 public:
  std::string path;
  size_t position = 0;
  bool opened = false;

  size_t size() { return todoFake::files.at(path).size(); }
  int read(void* output, size_t count) {
    if (todoFake::failRead) return -1;
    const auto& contents = todoFake::files.at(path);
    const size_t amount = std::min(count, contents.size() - position);
    std::memcpy(output, contents.data() + position, amount);
    position += amount;
    return static_cast<int>(amount);
  }
  size_t write(const uint8_t* bytes, size_t count) {
    ++todoFake::writes;
    const size_t amount =
        todoFake::writeRemaining < 0 ? count : std::min(count, static_cast<size_t>(todoFake::writeRemaining));
    todoFake::files.at(path).append(reinterpret_cast<const char*>(bytes), amount);
    if (todoFake::writeRemaining >= 0) todoFake::writeRemaining -= static_cast<int>(amount);
    return amount;
  }
  void flush() {}
  bool close() {
    opened = false;
    return !todoFake::failClose;
  }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  bool ready() { return todoFake::ready; }
  bool exists(const char* path) { return todoFake::files.count(path) != 0; }
  bool ensureDirectoryExists(const char*) { return !todoFake::failDirectory; }
  bool remove(const char* path) { return !todoFake::failRemove && todoFake::files.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (todoFake::failRenameAt == 0) {
      todoFake::failRenameAt = -1;
      return false;
    }
    if (todoFake::failRenameAt > 0) --todoFake::failRenameAt;
    if (!exists(from) || exists(to)) return false;
    todoFake::files[to] = todoFake::files[from];
    todoFake::files.erase(from);
    return true;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    if (!exists(path)) return false;
    file.path = path;
    file.position = 0;
    file.opened = true;
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    todoFake::files[path].clear();
    file.path = path;
    file.position = 0;
    file.opened = true;
    return true;
  }
};

#define Storage HalStorage::getInstance()
