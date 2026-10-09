#include <stdint.h>
typedef uint8_t byte;
#define ATOMIC_RESTORESTATE 0
#define ATOMIC_BLOCK(x) for (bool once = true; once; once = false)
static const int bufSize = 200;
volatile byte playBuf[bufSize], loadPos = 0, playPos = 0;
volatile bool playbackFinished = false;
enum TransportMode : byte { Playing, Paused, Stopped };
volatile byte transportMode = Playing;
volatile uint32_t floaded = 0;
uint32_t totalPos = 0;
uint16_t skipCnt = 0;
struct File {
  byte data[8]; byte pos = 0, size = 0;
  bool available() { return pos < size; }
  byte read() { return data[pos++]; }
} fp;
byte writes = 0, lastReg = 0, lastValue = 0;
void writeAY(byte reg, byte value) { ++writes; lastReg = reg; lastValue = value; }
#include "playback-under-test.h"

extern "C" {
volatile uint8_t test_done = 0, test_failure = 0, test_count = 0;
}
void expect(bool condition) {
  ++test_count;
  if (!condition && !test_failure) test_failure = test_count;
}
void reset(byte position = 0) {
  playPos = loadPos = position;
  playbackFinished = false; totalPos = floaded = 0; skipCnt = writes = 0;
}
void push(byte value) {
  playBuf[loadPos] = value;
  loadPos = (loadPos + 1) % bufSize;
}
int main() {
  reset(); push(8); playNotes();
  expect(playPos == 0 && totalPos == 0 && writes == 0);
  push(10); push(255); playNotes();
  expect(writes == 1 && lastReg == 8 && lastValue == 10 && totalPos == 3);
  playNotes(); expect(writes == 1 && totalPos == 3); // never replay on starvation
  reset(199); push(9); push(12); push(255); playNotes();
  expect(writes == 1 && playPos == 2 && lastReg == 9 && lastValue == 12);
  reset(); push(254); playNotes(); expect(playPos == 0 && skipCnt == 0);
  push(1); push(8); push(11); push(255); playNotes();
  expect(skipCnt == 4 && writes == 0);
  for (byte i = 0; i < 3; i++) playNotes();
  expect(skipCnt == 1 && writes == 0);
  playNotes(); expect(writes == 1 && lastValue == 11);
  reset(); push(254); push(0); push(8); push(9); push(255); playNotes();
  expect(skipCnt == 0 && writes == 1);
  reset(); push(253); playNotes(); expect(playbackFinished);
  push(8); push(9); playNotes(); expect(writes == 0);
  reset(); fp.size = 2; fp.pos = 0; fp.data[0] = 8; fp.data[1] = 7;
  while (loadNextByte()) {}
  expect(loadPos == 199 && floaded == 199 && fp.pos == 2);
  expect(!loadNextByte());
  playNotes(); expect(writes == 1 && lastValue == 7 && playbackFinished);
  reset(199); fp.size = 0; fp.pos = 0;
  expect(loadNextByte() && loadPos == 0 && playBuf[199] == 253);
  reset(); totalPos = 32767; push(8); push(10); push(255); playNotes();
  expect(totalPos == 32770UL);
  reset(); push(8); push(10); push(255); transportMode = Paused;
  playNotes(); expect(playPos == 0 && writes == 0 && totalPos == 0);
  transportMode = Stopped; playNotes(); expect(playPos == 0 && writes == 0);
  transportMode = Playing; playNotes(); expect(writes == 1 && lastValue == 10);
  test_done = 0xa5;
  while (true) {}
}
