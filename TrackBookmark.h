#pragma once
#include <stdint.h>

class StopHold {
 public:
  // 1: short click on release; 2: save once after five seconds.
  uint8_t update(bool held, uint32_t now) {
    if (held && !held_) { started_ = now; saved_ = false; }
    uint8_t event = 0;
    if (held && !saved_ && uint32_t(now - started_) >= 5000) {
      saved_ = true; event = 2;
    }
    if (!held && held_ && !saved_) event = 1;
    held_ = held;
    return event;
  }
 private:
  bool held_ = false, saved_ = false;
  uint32_t started_ = 0;
};

// EEPROM layout: marker at 3, display index at 4..5, filename at 6..261.
// Invalidating the marker first prevents a partially written bookmark being used.
template<class Memory>
void storeBookmark(Memory& memory, uint16_t index, const char* name) {
  memory.update(3, 0);
  memory.update(4, uint8_t(index));
  memory.update(5, uint8_t(index >> 8));
  for (uint16_t i = 0; i < 256; ++i) {
    memory.update(6 + i, uint8_t(name[i]));
    if (!name[i]) { memory.update(3, 0xA6); return; }
  }
}

template<class Memory>
bool bookmarkMatches(Memory& memory, const char* name) {
  if (memory.read(3) != 0xA6) return false;
  for (uint16_t i = 0; i < 256; ++i) {
    if (memory.read(6 + i) != uint8_t(name[i])) return false;
    if (!name[i]) return true;
  }
  return false;
}

template<class Memory, class List>
int restoreBookmark(Memory& memory, List& list, int count, char* name) {
  if (memory.read(3) == 0xA6) {
    for (int index = 0; index < count; ++index) {
      list.getFile(index);
      if (bookmarkMatches(memory, name)) return index;
    }
  }
  return 0;
}
