#include "TodoStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr char FILE_PATH[] = "/.crosspoint/todos.json";
constexpr char TEMP_PATH[] = "/.crosspoint/todos.tmp";
constexpr char BACKUP_PATH[] = "/.crosspoint/todos.bak";
constexpr size_t MAX_FILE_BYTES = 65536;

class JsonReader {
 public:
  JsonReader(HalFile& file, size_t length) : file(file), remaining(length) {}

  bool read(TodoListModel& model) {
    model.clear();
    if (!take('{')) return false;
    unsigned fields = 0;
    do {
      char key[10];
      if (!string(key, sizeof(key)) || !take(':')) return false;
      if (std::strcmp(key, "version") == 0 && !(fields & 1)) {
        if (!literal("1")) return false;
        fields |= 1;
      } else if (std::strcmp(key, "items") == 0 && !(fields & 2)) {
        if (!list(model)) return false;
        fields |= 2;
      } else {
        return false;
      }
    } while (take(','));
    if (fields != 3 || !take('}')) return false;
    whitespace();
    return peek() < 0 && !failed;
  }

 private:
  HalFile& file;
  size_t remaining;
  uint8_t buffer[128]{};
  size_t offset = 0;
  size_t buffered = 0;
  bool failed = false;

  int peek() {
    if (offset == buffered) {
      if (!remaining || failed) return -1;
      const size_t amount = std::min(remaining, sizeof(buffer));
      if (file.read(buffer, amount) != static_cast<int>(amount)) {
        failed = true;
        return -1;
      }
      remaining -= amount;
      buffered = amount;
      offset = 0;
    }
    return buffer[offset];
  }

  int next() {
    const int value = peek();
    if (value >= 0) ++offset;
    return value;
  }

  void whitespace() {
    while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') next();
  }

  bool take(char value) {
    whitespace();
    if (peek() != value) return false;
    next();
    return true;
  }

  bool literal(const char* value) {
    whitespace();
    while (*value) {
      if (next() != *value++) return false;
    }
    return true;
  }

  bool hex(uint32_t& value) {
    value = 0;
    for (unsigned i = 0; i < 4; ++i) {
      const int digit = next();
      value <<= 4;
      if (digit >= '0' && digit <= '9')
        value |= digit - '0';
      else if (digit >= 'a' && digit <= 'f')
        value |= digit - 'a' + 10;
      else if (digit >= 'A' && digit <= 'F')
        value |= digit - 'A' + 10;
      else
        return false;
    }
    return true;
  }

  static bool append(char* text, size_t capacity, size_t& length, uint8_t value) {
    if (length + 1 >= capacity || value == 0) return false;
    text[length++] = static_cast<char>(value);
    return true;
  }

  bool unicode(char* text, size_t capacity, size_t& length) {
    uint32_t point;
    if (!hex(point)) return false;
    if (point >= 0xd800 && point <= 0xdbff) {
      uint32_t low;
      if (next() != '\\' || next() != 'u' || !hex(low) || low < 0xdc00 || low > 0xdfff) return false;
      point = 0x10000 + ((point - 0xd800) << 10) + low - 0xdc00;
    } else if (point >= 0xdc00 && point <= 0xdfff) {
      return false;
    }
    if (point < 0x80) return append(text, capacity, length, point);
    if (point < 0x800) {
      return append(text, capacity, length, 0xc0 | (point >> 6)) &&
             append(text, capacity, length, 0x80 | (point & 0x3f));
    }
    if (point < 0x10000) {
      return append(text, capacity, length, 0xe0 | (point >> 12)) &&
             append(text, capacity, length, 0x80 | ((point >> 6) & 0x3f)) &&
             append(text, capacity, length, 0x80 | (point & 0x3f));
    }
    return append(text, capacity, length, 0xf0 | (point >> 18)) &&
           append(text, capacity, length, 0x80 | ((point >> 12) & 0x3f)) &&
           append(text, capacity, length, 0x80 | ((point >> 6) & 0x3f)) &&
           append(text, capacity, length, 0x80 | (point & 0x3f));
  }

  bool string(char* text, size_t capacity) {
    if (!take('"')) return false;
    size_t length = 0;
    for (;;) {
      int value = next();
      if (value < 0x20) return false;
      if (value == '"') {
        text[length] = '\0';
        return true;
      }
      if (value == '\\') {
        value = next();
        if (value == 'u') {
          if (!unicode(text, capacity, length)) return false;
          continue;
        }
        switch (value) {
          case '"':
          case '\\':
          case '/':
            break;
          case 'b':
            value = '\b';
            break;
          case 'f':
            value = '\f';
            break;
          case 'n':
            value = '\n';
            break;
          case 'r':
            value = '\r';
            break;
          case 't':
            value = '\t';
            break;
          default:
            return false;
        }
      }
      if (!append(text, capacity, length, value)) return false;
    }
  }

  bool entry(TodoListModel& model) {
    if (!take('{')) return false;
    char text[TodoListModel::MAX_TEXT_BYTES + 1]{};
    char key[10];
    bool completed = false;
    unsigned fields = 0;
    do {
      if (!string(key, sizeof(key)) || !take(':')) return false;
      if (std::strcmp(key, "text") == 0 && !(fields & 1)) {
        if (!string(text, sizeof(text))) return false;
        fields |= 1;
      } else if (std::strcmp(key, "completed") == 0 && !(fields & 2)) {
        whitespace();
        completed = peek() == 't';
        if (!literal(completed ? "true" : "false")) return false;
        fields |= 2;
      } else {
        return false;
      }
    } while (take(','));
    if (fields != 3 || !take('}') || !model.add(text)) return false;
    if (completed) model.toggle(model.openCount() - 1);
    return true;
  }

  bool list(TodoListModel& model) {
    if (!take('[')) return false;
    if (take(']')) return true;
    do {
      if (!entry(model)) return false;
    } while (take(','));
    return take(']');
  }
};

bool readModel(const char* path, TodoListModel& model) {
  HalFile file;
  if (!Storage.openFileForRead("TODO", path, file)) return false;
  const size_t length = file.size();
  if (!length || length > MAX_FILE_BYTES) return false;
  JsonReader reader(file, length);
  return reader.read(model);
}

class JsonWriter {
 public:
  explicit JsonWriter(HalFile& file) : file(file) {}

  bool write(const TodoListModel& model) {
    text("{\"version\":1,\"items\":[");
    for (size_t i = 0; i < model.count(); ++i) {
      const auto* item = model.item(i);
      if (i) byte(',');
      text("{\"text\":\"");
      for (const char* cursor = item->text; *cursor; ++cursor) {
        if (*cursor == '"' || *cursor == '\\') byte('\\');
        byte(*cursor);
      }
      text(item->completed ? "\",\"completed\":true}" : "\",\"completed\":false}");
    }
    text("]}\n");
    flush();
    return !failed;
  }

 private:
  HalFile& file;
  uint8_t buffer[128]{};
  size_t buffered = 0;
  bool failed = false;

  void flush() {
    if (buffered && !failed && file.write(buffer, buffered) != buffered) failed = true;
    buffered = 0;
  }
  void byte(char value) {
    if (failed) return;
    buffer[buffered++] = static_cast<uint8_t>(value);
    if (buffered == sizeof(buffer)) flush();
  }
  void text(const char* value) {
    while (*value && !failed) byte(*value++);
  }
};

bool writeModel(const TodoListModel& model) {
  HalFile file;
  if (!Storage.openFileForWrite("TODO", TEMP_PATH, file)) return false;
  JsonWriter writer(file);
  if (!writer.write(model)) return false;
  file.flush();
  // Close before verifying and renaming the temporary file.
  return file.close();
}

bool sameModel(const TodoListModel& left, const TodoListModel& right) {
  if (left.count() != right.count()) return false;
  for (size_t i = 0; i < left.count(); ++i) {
    if (left.item(i)->completed != right.item(i)->completed ||
        std::strcmp(left.item(i)->text, right.item(i)->text) != 0)
      return false;
  }
  return true;
}
}  // namespace

bool TodoStore::load() {
  status = LoadStatus::Error;
  primaryValid = false;
  recoveredFromTemporary = false;
  if (!Storage.ready()) {
    LOG_ERR("TODO", "Storage is unavailable");
    return false;
  }
  if (!Storage.exists(FILE_PATH) && !Storage.exists(BACKUP_PATH) && !Storage.exists(TEMP_PATH)) {
    items.clear();
    status = LoadStatus::Empty;
    return true;
  }

  // A bounded scratch model keeps the current list intact on parse failure without using the task stack.
  auto candidate = makeUniqueNoThrow<TodoListModel>();
  if (!candidate) {
    LOG_ERR("TODO", "OOM loading to-do list");
    return false;
  }
  static constexpr const char* PATHS[] = {FILE_PATH, BACKUP_PATH, TEMP_PATH};
  for (const char* path : PATHS) {
    if (!Storage.exists(path)) continue;
    if (!readModel(path, *candidate)) {
      LOG_ERR("TODO", "Invalid or unreadable to-do file: %s", path);
      continue;
    }
    items = *candidate;
    primaryValid = path == FILE_PATH;
    recoveredFromTemporary = path == TEMP_PATH;
    status = primaryValid ? LoadStatus::Loaded : LoadStatus::Recovered;
    LOG_INF("TODO", "Loaded %zu items from %s", items.count(), path);
    return true;
  }
  return false;
}

bool TodoStore::save() {
  if (isReadOnly() || !Storage.ready()) {
    LOG_ERR("TODO", "Cannot save an unavailable to-do list");
    return false;
  }
  auto verification = makeUniqueNoThrow<TodoListModel>();
  if (!verification) {
    LOG_ERR("TODO", "OOM verifying to-do list");
    return false;
  }
  if (recoveredFromTemporary) {
    if ((Storage.exists(BACKUP_PATH) && !Storage.remove(BACKUP_PATH)) || !Storage.rename(TEMP_PATH, BACKUP_PATH)) {
      LOG_ERR("TODO", "Failed to preserve recovered to-do list");
      return false;
    }
    recoveredFromTemporary = false;
  }
  if (!Storage.ensureDirectoryExists("/.crosspoint") || !writeModel(items) || !readModel(TEMP_PATH, *verification) ||
      !sameModel(items, *verification)) {
    LOG_ERR("TODO", "Failed to write and verify to-do list");
    return false;
  }

  bool movedPrimary = false;
  if (Storage.exists(FILE_PATH)) {
    if (primaryValid) {
      if ((Storage.exists(BACKUP_PATH) && !Storage.remove(BACKUP_PATH)) || !Storage.rename(FILE_PATH, BACKUP_PATH)) {
        LOG_ERR("TODO", "Failed to preserve previous to-do list");
        return false;
      }
      movedPrimary = true;
    } else if (!Storage.remove(FILE_PATH)) {
      LOG_ERR("TODO", "Failed to replace invalid to-do file");
      return false;
    }
  }
  primaryValid = false;
  if (!Storage.rename(TEMP_PATH, FILE_PATH)) {
    if (movedPrimary) primaryValid = Storage.rename(BACKUP_PATH, FILE_PATH);
    LOG_ERR("TODO", "Failed to install verified to-do list");
    return false;
  }
  primaryValid = true;
  status = LoadStatus::Loaded;
  return true;
}
