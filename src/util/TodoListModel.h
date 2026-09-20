#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

class TodoListModel {
 public:
  static constexpr size_t MAX_ITEMS = 64;
  static constexpr size_t MAX_TEXT_BYTES = 127;

  struct Item {
    char text[MAX_TEXT_BYTES + 1]{};
    bool completed = false;
  };

  size_t count() const { return itemCount; }
  size_t openCount() const { return incompleteCount; }
  const Item* item(size_t index) const { return index < itemCount ? &items[index] : nullptr; }

  bool add(const char* text) {
    if (itemCount == MAX_ITEMS) return false;
    Item next;
    if (!normalize(text, next.text)) return false;
    for (size_t i = itemCount; i > incompleteCount; --i) items[i] = items[i - 1];
    items[incompleteCount++] = next;
    ++itemCount;
    return true;
  }

  bool update(size_t index, const char* text) {
    if (index >= itemCount) return false;
    char normalized[MAX_TEXT_BYTES + 1];
    if (!normalize(text, normalized)) return false;
    std::memcpy(items[index].text, normalized, sizeof(normalized));
    return true;
  }

  bool toggle(size_t index) {
    if (index >= itemCount) return false;
    Item changed = items[index];
    changed.completed = !changed.completed;
    if (changed.completed) {
      for (size_t i = index; i + 1 < itemCount; ++i) items[i] = items[i + 1];
      items[itemCount - 1] = changed;
      --incompleteCount;
    } else {
      for (size_t i = index; i > incompleteCount; --i) items[i] = items[i - 1];
      items[incompleteCount++] = changed;
    }
    return true;
  }

  bool remove(size_t index) {
    if (index >= itemCount) return false;
    return removeSelected(uint64_t{1} << index) != 0;
  }

  size_t removeSelected(uint64_t selected) {
    size_t kept = 0;
    size_t open = 0;
    for (size_t i = 0; i < itemCount; ++i) {
      if (selected & (uint64_t{1} << i)) continue;
      if (kept != i) items[kept] = items[i];
      if (!items[kept].completed) ++open;
      ++kept;
    }
    const size_t removed = itemCount - kept;
    for (size_t i = kept; i < itemCount; ++i) items[i] = Item{};
    itemCount = kept;
    incompleteCount = open;
    return removed;
  }

  void clear() {
    for (size_t i = 0; i < itemCount; ++i) items[i] = Item{};
    itemCount = 0;
    incompleteCount = 0;
  }

 private:
  Item items[MAX_ITEMS]{};
  size_t itemCount = 0;
  size_t incompleteCount = 0;

  static bool whitespace(unsigned char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
  }

  static bool normalize(const char* input, char* output) {
    if (!input) return false;
    while (whitespace(static_cast<unsigned char>(*input))) ++input;
    size_t length = std::strlen(input);
    while (length && whitespace(static_cast<unsigned char>(input[length - 1]))) --length;
    if (!length || length > MAX_TEXT_BYTES) return false;

    for (size_t i = 0; i < length;) {
      const auto first = static_cast<unsigned char>(input[i++]);
      if (first < 0x20 || first == 0x7f) return false;
      if (first < 0x80) continue;
      size_t extra;
      uint32_t point;
      uint32_t minimum;
      if (first >= 0xc2 && first <= 0xdf) {
        extra = 1;
        point = first & 0x1f;
        minimum = 0x80;
      } else if (first >= 0xe0 && first <= 0xef) {
        extra = 2;
        point = first & 0x0f;
        minimum = 0x800;
      } else if (first >= 0xf0 && first <= 0xf4) {
        extra = 3;
        point = first & 0x07;
        minimum = 0x10000;
      } else {
        return false;
      }
      if (i + extra > length) return false;
      while (extra--) {
        const auto next = static_cast<unsigned char>(input[i++]);
        if ((next & 0xc0) != 0x80) return false;
        point = (point << 6) | (next & 0x3f);
      }
      if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
    }
    std::memset(output, 0, MAX_TEXT_BYTES + 1);
    std::memcpy(output, input, length);
    return true;
  }
};
