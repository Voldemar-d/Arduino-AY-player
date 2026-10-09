#include "../TrackBookmark.h"
#include <string.h>
typedef uint8_t byte;
extern "C" { volatile byte test_done = 0, test_failure = 0, test_count = 0; }
void expect(bool condition) { ++test_count; if (!condition && !test_failure) test_failure = test_count; }
struct Memory {
  byte data[262];
  int budget = 32767;
  byte read(int address) { return data[address]; }
  void update(int address, byte value) { if (budget-- > 0) data[address] = value; }
} memory;
char name[64];
struct List {
  const char* names[3];
  void getFile(int index) { strcpy(name, names[index]); }
} list = {{"FIRST.PSG", "TESTABC.PSG", "LAST.PSG"}};
int main() {
  memset(memory.data, 255, sizeof(memory.data));
  expect(restoreBookmark(memory, list, 3, name) == 0);
  storeBookmark(memory, 1, "TESTABC.PSG");
  expect(memory.read(3) == 0xA6 && memory.read(4) == 1 && memory.read(5) == 0);
  expect(bookmarkMatches(memory, "TESTABC.PSG"));
  expect(!bookmarkMatches(memory, "TESTAB.PSG"));
  expect(!bookmarkMatches(memory, "TESTABC.PSGX"));
  expect(restoreBookmark(memory, list, 3, name) == 1);
  list.names[0] = "TESTABC.PSG"; list.names[1] = "FIRST.PSG";
  expect(restoreBookmark(memory, list, 3, name) == 0);
  list.names[0] = "NEW.PSG";
  expect(restoreBookmark(memory, list, 3, name) == 0);
  expect(restoreBookmark(memory, list, 0, name) == 0);
  storeBookmark(memory, 0, "FIRST.PSG");
  memory.budget = 5; storeBookmark(memory, 2, "LAST.PSG");
  expect(memory.read(3) == 0 && !bookmarkMatches(memory, "LAST.PSG"));
  memory.budget = 32767; storeBookmark(memory, 2, "LAST.PSG");
  expect(restoreBookmark(memory, list, 3, name) == 2);
  StopHold hold;
  expect(hold.update(true, 0) == 0);
  expect(hold.update(true, 4999) == 0);
  expect(hold.update(true, 5000) == 2);
  expect(hold.update(true, 10000) == 0);
  expect(hold.update(false, 10001) == 0);
  expect(hold.update(true, 10010) == 0);
  expect(hold.update(false, 10110) == 1);
  expect(hold.update(false, 10120) == 0);
  StopHold rollover;
  rollover.update(true, 0xfffffff0UL);
  expect(rollover.update(true, 4983) == 0);
  expect(rollover.update(true, 4984) == 2);
  test_done = 0xa5;
  while (true) {}
}
