#include "../ButtonControls.h"
#include "../TrackBookmark.h"
typedef uint8_t byte;
#define ATOMIC_RESTORESTATE 0
#define ATOMIC_BLOCK(x) for (bool once = true; once; once = false)
enum TransportMode : byte { Playing, Paused, Stopped };
volatile byte transportMode = Playing;
volatile bool playbackFinished = false, displayDue = false;
bool demoMode = false, randMode = true, bSwitchMeter = false, restartRequested = false;
const byte SAVE_DEMO_MODE = 2, SAVE_RAND_MODE = 1;
int filesCount = 8, fileNum = 5, fileNext = 0;
byte ayRegisters[14] = {}, output[14] = {}, inBtn = 255, raw = 255;
char fname[] = "TEST.PSG";
uint32_t now = 0;
byte resets = 0, loads = 0;
ButtonControls buttons;
struct Settings {
  byte commits = 0;
  void update(int address, byte value) { if (address == 3 && value == 0xA6) ++commits; }
} EEPROM;
struct Display { void clear() {} } oled;
void dispMsg(const char*) {}
uint32_t millis() { return now; }
byte in_165_byte() { return raw; }
void showMode(bool) {}
void showFile() {}
void displayOLED() {}
void writeAYHardware(byte reg, byte value) { output[reg] = value; }
void muteAY();
void resumePlayback();
void resetAY() { ++resets; for (byte i = 0; i < 14; ++i) ayRegisters[i] = output[i] = 0; }
void loadRandomFile(bool manual) { ++loads; if (!manual && randMode) fileNum = 7; playbackFinished = false; }
bool loadNextByte() { return false; }
#include "transport-under-test.h"
extern "C" { volatile byte test_done = 0, test_failure = 0, test_count = 0; }
void expect(bool condition) { ++test_count; if (!condition && !test_failure) test_failure = test_count; }
void sample(byte value, uint32_t dt) { raw = value; now += dt; loop(); }
void press(byte bit) {
  byte value = 255 ^ (1 << bit);
  sample(value, 5); sample(value, 25);
  sample(255, 5); sample(255, 25);
}
int main() {
  ayRegisters[7] = 0x38; ayRegisters[8] = 10; ayRegisters[9] = 11; ayRegisters[10] = 12;
  press(4); expect(transportMode == Paused && !playbackFinished && loads == 0);
  expect(output[7] == 0x3F && output[8] == 0 && ayRegisters[8] == 10);
  press(5); expect(transportMode == Playing && output[7] == 0x38 && output[8] == 10 && loads == 0);
  press(4); press(4); expect(transportMode == Playing && loads == 0);
  press(5); expect(transportMode == Stopped && playbackFinished && loads == 0);
  for (byte i = 0; i < 10; ++i) sample(255, 25);
  expect(transportMode == Stopped && loads == 0);
  press(1); expect(fileNum == 6 && transportMode == Stopped && loads == 1);
  expect(output[8] == 0);
  press(5); expect(fileNum == 6 && transportMode == Playing && loads == 2);
  press(4); press(3); expect(fileNum == 3 && transportMode == Paused && loads == 3);
  press(5); expect(fileNum == 3 && transportMode == Playing && loads == 3);
  press(5); press(0); expect(fileNum == 2 && transportMode == Stopped);
  press(4); expect(transportMode == Stopped);
  press(5); expect(fileNum == 2 && transportMode == Playing);
  playbackFinished = true; loop(); expect(fileNum == 7 && transportMode == Playing);
  randMode = false; playbackFinished = true; loop(); expect(fileNum == 0);
  sample(223, 5); sample(223, 25); sample(223, 4999);
  expect(transportMode == Playing && EEPROM.commits == 0);
  sample(223, 5); expect(transportMode == Stopped && EEPROM.commits == 1);
  sample(223, 1000); expect(EEPROM.commits == 1);
  sample(255, 5); sample(255, 25); expect(transportMode == Stopped);
  test_done = 0xa5;
  while (true) {}
}
