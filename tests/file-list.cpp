#include <stdint.h>
#include <string.h>
typedef uint8_t byte;
static const uint32_t nMaxAdr = 0x20000, nMaxFiles = 32767;
static const byte szN = 3, szR = 2, szStruct = 5, fileExtLen = 4;
const char* supportedFileExt = ".psg";
char fname[256];
uint32_t fsize = 0, fnlen = 0;
unsigned long seed = 1;
long random(long n) { seed = seed * 1664525UL + 1013904223UL; return (seed >> 1) % n; }
long random(long low, long high) { return low + random(high - low); }
struct SRAM {
  bool connected = true;
  byte memory[512] = {};
  uint16_t map(uint32_t address) { return address < 256 ? address : 256 + address - (nMaxAdr - 256); }
  byte readByte(uint32_t address) { return connected ? memory[map(address)] : 255; }
  void readBlock(uint32_t address, uint16_t count, void* buffer) {
    for (uint16_t i = 0; i < count; ++i) ((byte*)buffer)[i] = readByte(address + i);
  }
  void writeBlock(uint32_t address, uint16_t count, void* buffer) {
    if (connected) for (uint16_t i = 0; i < count; ++i) memory[map(address + i)] = ((byte*)buffer)[i];
  }
} sram;
struct Entry { const char* name; bool hidden, directory; uint16_t size; };
const Entry entries[] = {
  {"System", true, true, 0}, {"1.PSG", false, false, 100},
  {"notes.txt", false, false, 10}, {"empty.psg", false, false, 0},
  {"longer.PSG", false, false, 200}, {"hidden.psg", true, false, 100},
  {"subdir.psg", false, true, 0}, {"TESTABC.PSG", false, false, 300}
};
const int O_READ = 0;
struct SdFile {
  int pos = -1;
  bool open(const char*, int) { pos = -1; return true; }
  bool openNext(SdFile* dir, int) { pos = ++dir->pos; return pos < 8; }
  bool getName(char* name, unsigned int length) { strncpy(name, entries[pos].name, length); return true; }
  uint32_t fileSize() { return entries[pos].size; }
  bool isHidden() { return entries[pos].hidden; }
  bool isDir() { return entries[pos].directory; }
  void close() {}
};
struct SD { void errorHalt(const char*) {} } sd;
void dispMsg(const char*) {}
#include "file-list-under-test.h"
extern "C" { volatile byte test_done = 0, test_failure = 0, test_count = 0; }
void expect(bool condition) { ++test_count; if (!condition && !test_failure) test_failure = test_count; }
int main() {
  const char* expected[] = {"1.PSG", "longer.PSG", "TESTABC.PSG"};
  for (byte connected = 0; connected < 2; ++connected) {
    sram.connected = connected;
    CFileList list;
    list.DetectSram(); expect(list.usesSram() == bool(connected));
    list.GetList(); expect(list.getCount() == 3);
    for (int i = 0; i < 3; ++i) {
      list.getFile(i); expect(!strcasecmp(fname, expected[i]));
    }
    list.getFile(-1); expect(fname[0] == 0);
    list.getFile(3); expect(fname[0] == 0);
    uint16_t last = 65535;
    for (byte round = 0; round < 10; ++round) {
      byte seen = 0;
      for (byte i = 0; i < 3; ++i) {
        uint16_t index = list.getRandom();
        expect(index < 3 && !(seen & (1 << index)) && (i || index != last));
        seen |= 1 << index; last = index;
        list.getFile(index); expect(!strcasecmp(fname, expected[index]));
      }
      expect(seen == 7);
    }
  }
  test_done = 0xa5;
  while (true) {}
}
