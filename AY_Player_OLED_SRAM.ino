// Arduino Pro Micro project: chiptune player of PSG files from SD card
// on AY-3-8910 chip with OLED display, SRAM for storing file names
// and 8 buttons for switching between modes and songs

#include <EEPROM.h>
#include <util/atomic.h>
#include "ButtonControls.h"
#include "TrackBookmark.h"
#define SAVE_VOL_MODE 0
#define SAVE_RAND_MODE 1
#define SAVE_DEMO_MODE 2

#include <FrequencyGenerator.h>
FrequencyGenerator fg; // for Timer4 1.78 MHz

#include "aymeter.h"
SSD1306TextVol oled(3); // draw volume indicator in 8-pixel row number 4 (index 3)
#define I2C_ADDRESS 0x3C

#include <SRAM_23LC.h>
#include <SdFat.h>
static const byte SD_CS_PIN = 10, // SD card chip select (CS)
                  SRAM_CS_PIN = A2; // SRAM chip select (CS)
static const uint32_t nMaxAdr = 0x20000, nMaxFiles = 32767;
SRAM_23LC sram(&SPI, SRAM_CS_PIN, SRAM_23LC1024);

static const byte RANDOM_SEED_PIN = A1, STATUS_LED_PIN = LED_BUILTIN_TX;

class CStatusLed {
  public:
    CStatusLed() {
      m_ledCnt = m_cntMax = 0;
      m_ledOn = m_ledOk = false;
      pinMode(STATUS_LED_PIN, OUTPUT);
    }
    void SetOK(bool OK) {
      m_ledOk = OK;
      m_cntMax = (OK ? 50 : 20);
      digitalWrite(STATUS_LED_PIN, LOW);
      m_ledOn = true;
    }
    void ShowStatus() {
      if (m_cntMax < 1) return;
      m_ledCnt++;
      if (m_ledCnt >= m_cntMax) {
        m_ledCnt = 0;
        m_ledOn = !m_ledOn;
        digitalWrite(STATUS_LED_PIN, m_ledOn ? HIGH : LOW);
      }
    }
  private:
    byte m_ledCnt, m_cntMax;
    bool m_ledOn, m_ledOk;
};

CStatusLed sled;

static const byte OUT_SHIFT_DATA_PIN = 7;  // -> pin 14 of 74HC595 - data input, slave in SI (DS)
static const byte OUT_SHIFT_LATCH_PIN = 6; // -> pin 12 of 74HC595 - data output latch (ST_CP)
static const byte OUT_SHIFT_CLOCK_PIN = 4; // -> pin 11 of 74HC595 - clock pin SCK (SH_CP)

// bits of E/D ports, not pin numbers!
static const byte OUT_SHIFT_DATA_BIT = 6;  // pin 7 (PE6) => DS pin 14
static const byte OUT_SHIFT_LATCH_BIT = 7; // pin 6 (PD7) => ST_CP pin 12
static const byte OUT_SHIFT_CLOCK_BIT = 4; // pin 4 (PD4) => SH_CP pin 11

void setupShiftOut() {
  pinMode(OUT_SHIFT_LATCH_PIN, OUTPUT);
  pinMode(OUT_SHIFT_DATA_PIN, OUTPUT);
  pinMode(OUT_SHIFT_CLOCK_PIN, OUTPUT);
  digitalWrite(OUT_SHIFT_LATCH_PIN, HIGH);
}

static const byte IN_SHIFT_LATCH_PIN = 9; // -> pin 1 of 74HC165 - parallel load (PL)
static const byte IN_SHIFT_CLOCK_PIN = 8; // -> pin 2 of 74HC165 - shift clock (CP)
static const byte IN_SHIFT_DATA_PIN = A0; // -> pin 9 of 74HC165 - data input, slave in SI (DS)

// bits of B/F ports, not pin numbers!
static const byte IN_SHIFT_DATA_BIT = 7;  // pin A0 (PF7) => DS pin 9
static const byte IN_SHIFT_LATCH_BIT = 5; // pin 9 (PB5) => PL pin 1
static const byte IN_SHIFT_CLOCK_BIT = 4; // pin 8 (PB4) => CP pin 2

void setupShiftIn() {
  pinMode(IN_SHIFT_DATA_PIN, INPUT);
  pinMode(IN_SHIFT_CLOCK_PIN, OUTPUT);
  pinMode(IN_SHIFT_LATCH_PIN, OUTPUT);
  digitalWrite(IN_SHIFT_LATCH_PIN, HIGH);
  digitalWrite(IN_SHIFT_CLOCK_PIN, LOW);
}

byte inBtn = 255, outLo = 0, outHi = 0;
ButtonControls buttons;
bool bSwitchMeter = false;

void dispMsg(const char* pstr) {
  oled.setCursor(0, 0);
  oled.print(pstr);
}

const char *supportedFileExt = ".psg";
static const byte fileExtLen = strlen(supportedFileExt);
static const int nTxtLen = 40;
char fname[256], fperc[8], fmode[22];
uint32_t fsize = 0, fnlen;
volatile uint32_t floaded = 0;

// structure:
// { uint24 offset/length, uint16 random number }
static const byte szN = 3, szR = sizeof(uint16_t),
                  szStruct = szN + szR;
SdFat sd;

class CFileList {
    uint16_t m_nFiles, m_nRandom;
    uint16_t m_randomOffset = 0, m_randomStep = 1;
    bool m_useSram = false;
    uint32_t m_nFreeAdrLo, m_nFreeAdrHi;
    bool isPlayable(SdFile& entry) const {
      if (!entry.getName(fname, sizeof(fname))) return false;
      fsize = entry.fileSize();
      fnlen = strlen(fname);
      return !entry.isHidden() && !entry.isDir() && fnlen > fileExtLen &&
             !strcasecmp(fname + fnlen - fileExtLen, supportedFileExt) && fsize > 0;
    }
    bool checkFile(SdFile entry) {
      if (m_nFiles >= nMaxFiles) return false;
      bool bOK = isPlayable(entry);
      if (!m_useSram) return bOK;
      if (bOK) {
        if (m_nFreeAdrLo + fnlen + szStruct * 2 < m_nFreeAdrHi && fnlen > fileExtLen) {
          fnlen -= fileExtLen;
          m_nFreeAdrHi -= szStruct;
          sram.writeBlock(m_nFreeAdrHi, szN, (byte*)((0 == m_nFreeAdrLo) ? &fnlen : &m_nFreeAdrLo));
          //
          sram.writeBlock(m_nFreeAdrLo, fnlen, (byte*)fname);
          m_nFreeAdrLo += fnlen;
          //
          m_nFreeAdrHi -= szStruct;
          sram.writeBlock(m_nFreeAdrHi, szN, (byte*)&m_nFreeAdrLo);
          m_nFreeAdrHi += szStruct;
        }
        else
          bOK = false;
      }
      return bOK;
    }
    void FillRandom(uint16_t nNotFirst = 0xFFFF)
    {
      const uint16_t n = m_nFiles;
      if (n < 2) return;
      if (!m_useSram) {
        // A coprime step visits every track once without an SRAM permutation.
        m_randomOffset = random(n);
        if (m_randomOffset == nNotFirst) m_randomOffset = (m_randomOffset + 1) % n;
        m_randomStep = random(1, n);
        for (;;) {
          uint16_t a = m_randomStep, b = n;
          while (b) { uint16_t r = a % b; a = b; b = r; }
          if (a == 1) break;
          if (++m_randomStep >= n) m_randomStep = 1;
        }
        m_nRandom = 0;
        return;
      }
      uint16_t i, j, t, v;
      uint32_t adr = nMaxAdr - szR, adr1;
      for (i = 0; i < n; i++) {
        sram.writeBlock(adr, szR, (byte*)&i);
        adr -= szStruct;
      }
      for (i = 0; i < n; i++) {
        adr = nMaxAdr - szR - uint32_t(i) * szStruct;
        sram.readBlock(adr, szR, (byte*)&t); // t = a[i]
        j = random(n - i) + i;
        adr1 = nMaxAdr - szR - uint32_t(j) * szStruct;
        sram.readBlock(adr1, szR, (byte*)&v); // v = a[j]
        sram.writeBlock(adr, szR, (byte*)&v); // a[i] = a[j]
        sram.writeBlock(adr1, szR, (byte*)&t); // a[j] = t
      }
      adr = nMaxAdr - szR;
      sram.readBlock(adr, szR, (byte*)&v);
      if (v == nNotFirst) { // swap first and last elements
        adr1 = nMaxAdr - szR - uint32_t(m_nFiles - 1) * szStruct;
        sram.readBlock(adr1, szR, (byte*)&t);
        sram.writeBlock(adr, szR, (byte*)&t);
        sram.writeBlock(adr1, szR, (byte*)&v);
      }
      m_nRandom = 0;
    }
  public:
    CFileList() {
      Reset();
    }
    void Reset() {
      m_nFiles = m_nRandom = 0;
      m_nFreeAdrLo = 0;
      m_nFreeAdrHi = nMaxAdr;
    }
    void DetectSram() {
      const byte lo[] = {0x35, 0xCA, 0x69, 0x96};
      const byte hi[] = {0xA7, 0x58, 0xC3, 0x3C};
      sram.writeBlock(0, 4, (void*)lo);
      sram.writeBlock(nMaxAdr - 4, 4, (void*)hi);
      m_useSram = true;
      for (byte i = 0; i < 4; i++) {
        if (sram.readByte(i) != lo[i] || sram.readByte(nMaxAdr - 4 + i) != hi[i])
          m_useSram = false;
      }
    }
    bool usesSram() const { return m_useSram; }
    void GetList() {
      Reset();
      SdFile dir;
      if (dir.open("/", O_READ)) {
        SdFile file;
        while (file.openNext(&dir, O_READ)) {
          if (checkFile(file))
            m_nFiles++;
          file.close();
        }
        if (m_nFiles < 1)
          dispMsg("Files not found");
        else
          FillRandom();
        dir.close();
      }
      else {
        dispMsg("Open root failed");
        sd.errorHalt("Open root failed");
      }
    }
    int getCount() const {
      return m_nFiles;
    }
    void getFile(int index) const {
      fname[0] = 0;
      if (index < 0 || uint16_t(index) >= m_nFiles)
        return;
      if (!m_useSram) {
        SdFile dir, entry;
        if (!dir.open("/", O_READ)) return;
        int current = 0;
        bool found = false;
        while (entry.openNext(&dir, O_READ)) {
          if (isPlayable(entry) && current++ == index) found = true;
          entry.close();
          if (found) break;
        }
        dir.close();
        if (!found) fname[0] = 0;
        return;
      }
      uint32_t adr = nMaxAdr - (uint32_t(index) + 1) * szStruct, offset = 0;
      fsize = fnlen = 0;
      fname[0] = 0;
      sram.readBlock(adr, szN, (byte*)&offset);
      fnlen = offset;
      if (index > 0) {
        adr -= szStruct;
        uint32_t offset1 = 0;
        sram.readBlock(adr, szN, (byte*)&offset1);
        fnlen = offset1 - offset;
      }
      if (0 == index)
        offset = 0;
      //
      if (fnlen > sizeof(fname) - fileExtLen - 1 || offset + fnlen > nMaxAdr) return;
      sram.readBlock(offset, fnlen, (byte*)fname);
      fname[fnlen] = 0;
      strcat(fname, supportedFileExt);
    }
    uint16_t getRandom() {
      uint16_t rnd = 0;
      if (m_nFiles > 1) {
        if (!m_useSram) {
          if (m_nRandom >= m_nFiles) {
            uint16_t last = (m_randomOffset + uint32_t(m_nFiles - 1) * m_randomStep) % m_nFiles;
            FillRandom(last);
          }
          rnd = (m_randomOffset + uint32_t(m_nRandom++) * m_randomStep) % m_nFiles;
          return rnd;
        }
        uint32_t adr = nMaxAdr - szR - uint32_t(szStruct) * m_nRandom;
        if (m_nRandom >= m_nFiles) {
          adr = nMaxAdr - szR - uint32_t(szStruct) * (m_nFiles - 1);
          sram.readBlock(adr, szR, (byte*)&rnd);
          FillRandom(rnd);
        }
        adr = nMaxAdr - szR - uint32_t(szStruct) * m_nRandom;
        sram.readBlock(adr, szR, (byte*)&rnd);
        m_nRandom++;
      }
      return rnd;
    }
};

CFileList files;

enum eVolMode { // A/B/C volume indicator mode:
  eVolChars,  // > [] <
  eVolBars,   // bars
  eVolNotes,  // note/frequency
  eVolFullScreen,  // fullscreen note/frequency
  eVolTotal,
};

static const int bufSize = 200;
volatile bool demoMode = false;
bool randMode = true;
volatile bool displayDue = false;
byte nVolMode = eVolChars;
static const int demoLen = 10000, demoFadeLen = 500;
SdFile fp;
volatile byte volumeA, volumeB, volumeC;
volatile uint16_t divA, divB, divC;
volatile byte playBuf[bufSize];
volatile bool playbackFinished = true;
enum TransportMode : byte { Playing, Paused, Stopped };
volatile byte transportMode = Playing;
byte ayRegisters[14] = {};
bool restartRequested = false;
int filesCount;
int fileNum, fileNext = 0;
volatile byte loadPos = 0;
volatile byte playPos = 0;
uint16_t skipCnt = 0;
uint32_t totalPos = 0;
byte volumeScale = 255;

void setupDisplay() {
  oled.begin(&Adafruit128x32, I2C_ADDRESS);
  oled.setFont(Adafruit5x7);
  oled.clear();
}

void setupTimer() { // 50 Hz
  cli();
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS12);
  TIMSK1 = _BV(OCIE1A);
  TCNT1 = 0;
  OCR1A = F_CPU / 256 / 50 - 1;
  sei();
}

void loadSettings() {
  nVolMode = EEPROM.read(SAVE_VOL_MODE);
  if (nVolMode >= eVolTotal)
    nVolMode = eVolChars;
  byte n = EEPROM.read(SAVE_RAND_MODE);
  randMode = (n > 0);
  n = EEPROM.read(SAVE_DEMO_MODE);
  if (n > 1) n = 0;
  demoMode = (n != 0);
};

void setup() {
  loadSettings();
  setupDisplay();
  setupShiftOut();
  setupShiftIn();
  // Both SPI devices must be deselected before either is initialized.
  digitalWrite(SD_CS_PIN, HIGH); pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SRAM_CS_PIN, HIGH); pinMode(SRAM_CS_PIN, OUTPUT);
  generateSeed();
  fg.set(1780000); // set Timer4 for AY clock, pin #5 output mode is set inside
  resetAY();
  delay(500);
  initSD();
  sled.SetOK(filesCount > 0);
  fileNum = restoreBookmark(EEPROM, files, filesCount, fname);
  loadRandomFile(true);
  setupTimer();
}

void generateSeed() {
  unsigned long seed = seedOut(31);
  /*
    Serial.print("Seed = ");
    Serial.println(seed);
  */
  randomSeed(seed);
}
unsigned int bitOut(void) {
  static unsigned long firstTime = 1, prev = 0;
  unsigned long bit1 = 0, bit0 = 0, x = 0, limit = 99;
  if (firstTime)
  {
    firstTime = 0;
    prev = analogRead(RANDOM_SEED_PIN);
  }
  while (limit--)
  {
    x = analogRead(RANDOM_SEED_PIN);
    bit1 = (prev != x ? 1 : 0);
    prev = x;
    x = analogRead(RANDOM_SEED_PIN);
    bit0 = (prev != x ? 1 : 0);
    prev = x;
    if (bit1 != bit0)
      break;
  }
  return bit1;
}
unsigned long seedOut(unsigned int noOfBits) {
  // return value with 'noOfBits' random bits set
  unsigned long seed = 0;
  for (unsigned int i = 0; i < noOfBits; ++i)
    seed = (seed << 1) | bitOut();
  return seed;
}

#define BIT_AY_RESET 0
#define BIT_AY_BC1 1
#define BIT_AY_BDIR 2

void sendAY() {
  out_595_word(outLo, outHi);
}

void resetAY() {
  volumeA = volumeB = volumeC = 0;
  divA = divB = divC = 0;
  volumeScale = 255;

  bitClear(outHi, BIT_AY_BC1); // reset BC1
  bitClear(outHi, BIT_AY_BDIR); // reset BDIR
  bitClear(outHi, BIT_AY_RESET); // reset AY
  sendAY();
  delay(100);
  bitSet(outHi, BIT_AY_RESET); // unreset AY
  sendAY();
  delay(100);

  for (int i = 0; i < 16; i++) writeAY(i, 0);
}

void setVolume(byte scale) {
  volumeScale = scale;
  writeAY(8, volumeA);
  writeAY(9, volumeB);
  writeAY(10, volumeC);
}

void writeAY(const byte port, byte data) {
  if (port < 14) ayRegisters[port] = data;
  writeAYHardware(port, data);
}

void writeAYHardware(const byte port, byte data) { // write data to AY register
  if (port == 8 || port == 9 || port == 10) {
    if (port == 8) volumeA = data;
    if (port == 9) volumeB = data;
    if (port == 10) volumeC = data;
    // Bit 4 selects the AY envelope; preserve it until fading begins.
    if (volumeScale != 255) data = ((data & 16) ? 15 : (data & 15)) * uint16_t(volumeScale) / 255;
  }
  if (port == 0)
    *((volatile byte*)&divA) = data;
  else if (port == 1)
    *((volatile byte*)&divA + 1) = data & 0xF;
  else if (port == 2)
    *((volatile byte*)&divB) = data;
  else if (port == 3)
    *((volatile byte*)&divB + 1) = data & 0xF;
  else if (port == 4)
    *((volatile byte*)&divC) = data;
  else if (port == 5)
    *((volatile byte*)&divC + 1) = data & 0xF;

  bitClear(outHi, BIT_AY_BC1); // set zero to BC1, BDIR
  bitClear(outHi, BIT_AY_BDIR);
  sendAY();

  outLo = (port & B00001111); // set AY port 0...15 to D0...D3, set zero to D4...D7
  sendAY();

  bitSet(outHi, BIT_AY_BC1); // set 1 to BC1, BDIR
  bitSet(outHi, BIT_AY_BDIR);
  sendAY();
  delayMicroseconds(5);

  bitClear(outHi, BIT_AY_BC1); // set zero to BC1, BDIR
  bitClear(outHi, BIT_AY_BDIR);
  sendAY();
  delayMicroseconds(5);

  outLo = data; // set data bits to D0...D7
  sendAY();
  delayMicroseconds(5);

  bitSet(outHi, BIT_AY_BDIR); // set 1 to BDIR
  sendAY();
  delayMicroseconds(5);

  bitClear(outHi, BIT_AY_BC1); // set zero to BC1, BDIR
  bitClear(outHi, BIT_AY_BDIR);
  sendAY();
  delayMicroseconds(5);
}

void initSD() {
  if (!sd.begin(SD_CS_PIN, SD_SCK_MHZ(50))) {
    dispMsg("SD error");
    sled.SetOK(false);
    sd.initErrorHalt();
  }
  sram.begin();
  files.DetectSram();
  loadDirectory();
}

void loadDirectory() {
  files.GetList();
  filesCount = files.getCount();
}

void loadRandomFile(bool manual) {
  if (filesCount < 1) return;
  if (randMode && !manual)
    fileNum = files.getRandom();
  files.getFile(fileNum);
  SdFile file;
  if (!fname[0] || !file.open(fname, O_READ)) {
    dispMsg("File open error");
    sled.SetOK(false);
    filesCount = 0;
    return;
  }
  fsize = file.fileSize();
  playFile(file);
}

void showMode(bool bSetCursor) {
  sprintf (fmode, "Mode: %s  Demo: %s", randMode ? "rand" : "seq", demoMode ? "on" : "off");
  if (bSetCursor) {
    oled.setCursor(0, 0);
    oled.print(fmode);
    oled.clearToEOL();
    oled.setCursor(0, 3);
  }
  else
    oled.println(fmode);
}

void showName(bool bSetCursor) {
  if (bSetCursor)
    oled.setCursor(0, 1);
  oled.print("File: ");
  char buf[8]; sprintf(buf, "%d", fileNum + 1);
#ifdef FREQ_TWO_ROWS
  if (nVolMode < eVolNotes) {
#else
  {
#endif
    oled.print(buf);
    oled.print("/");
    sprintf(buf, "%d ", filesCount);
    oled.print(buf);
    oled.print(fsize / 1024);
    oled.print("kB"); oled.clearToEOL();
    oled.println();
    oled.print("Name: ");
  }
  oled.print(fname); oled.clearToEOL();
  oled.println();
#ifdef FREQ_TWO_ROWS
  if (eVolNotes == nVolMode) {
    oled.print(buf);
    oled.print("    ");
  }
#endif // FREQ_TWO_ROWS
}

void showFile() {
  oled.clear();
  if (nVolMode != eVolFullScreen || transportMode != Playing) {
    showMode(false);
    showName(false);
  }
  if (nVolMode > eVolChars)
    oled.vreset();
}

void playFile(SdFile entry) {
  // Called only while playback is stopped, so the ISR cannot touch the file state.
  fp.close();
  fp = entry;
  loadPos = playPos = 0;
  totalPos = floaded = 0;
  skipCnt = 0;
  if (fp.fileSize() < 16 || fp.read() != 'P' || fp.read() != 'S' ||
      fp.read() != 'G' || fp.read() != 0x1A || !fp.seekSet(16)) {
    dispMsg("Bad PSG header");
    filesCount = 0;
    sled.SetOK(false);
    return;
  }
  showFile();
  while (loadNextByte()) {}
  playbackFinished = false;
}

bool loadNextByte() {
  byte next = loadPos + 1;
  if (next == bufSize) next = 0;
  if (next == playPos) return false;
  byte b = fp.available() ? fp.read() : 0xFD;
  playBuf[loadPos] = b;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    loadPos = next;
    floaded++;
  }
  return true;
}

bool isNextByteAvailable() {
  return playPos != loadPos;
}

byte getNextByte() {
  if (!isNextByteAvailable()) return 0;
  byte b = playBuf[playPos++];
  if (playPos == bufSize) playPos = 0;
  totalPos++;
  return b;
}

void checkDemo() {
  if (transportMode == Playing && demoMode && totalPos >= demoLen && !playbackFinished) {
    uint32_t demoPos = totalPos - demoLen;
    if (demoPos >= demoFadeLen) {
      setVolume(0);
      playbackFinished = true;
    } else setVolume(255 - demoPos * 255 / demoFadeLen);
  }
}

void pollButtons() {
  static uint32_t lastPoll = 0;
  const uint32_t now = millis();
  if (uint32_t(now - lastPoll) < 5) return;
  lastPoll = now;
  inBtn = in_165_byte();
  ButtonControls::Events events = buttons.update(inBtn, now);
  static StopHold stopHold;
  byte stopEvent = stopHold.update(buttons.pressedMask() & 0x20, now);
  events.navigation &= ~0x20;
  if (stopEvent == 1) events.navigation |= 0x20;
  if (stopEvent == 2 && filesCount > 0) {
    transportMode = Stopped;
    playbackFinished = true;
    muteAY();
    storeBookmark(EEPROM, fileNum, fname);
    oled.clear(); dispMsg("SAVED");
  }
  if (events.mode == 3) bSwitchMeter = true;
  else if (events.mode == 2) {
    demoMode = !demoMode;
    showMode(true);
    EEPROM.update(SAVE_DEMO_MODE, demoMode ? 1 : 0);
  } else if (events.mode == 1) {
    randMode = !randMode;
    showMode(true);
    EEPROM.update(SAVE_RAND_MODE, randMode ? 1 : 0);
  }
  if (events.navigation && filesCount > 0) {
    if (events.navigation & 0x20) {
      if (transportMode == Playing) {
        transportMode = Stopped;
        playbackFinished = true;
        muteAY();
      } else if (transportMode == Paused) resumePlayback();
      else { restartRequested = true; transportMode = Playing; playbackFinished = true; fileNext = 0; }
      showFile();
    } else if (events.navigation & 0x10) {
      if (transportMode == Playing) { transportMode = Paused; muteAY(); }
      else if (transportMode == Paused) resumePlayback();
      showFile();
    }
    const int8_t steps[] = {-1, 1, -5, 5};
    for (byte i = 0; i < 4; i++) {
      if (events.navigation & (1 << i)) { fileNext = steps[i]; break; }
    }
    if (fileNext) playbackFinished = true;
  }
}

void muteAY() {
  writeAYHardware(7, 0x3F);
  for (byte reg = 8; reg <= 10; reg++) writeAYHardware(reg, 0);
}

void resumePlayback() {
  // The timer cannot consume commands until all saved output registers are restored.
  for (byte reg = 7; reg <= 10; reg++) writeAYHardware(reg, ayRegisters[reg]);
  transportMode = Playing;
}

void playNotes() {
  if (playbackFinished || transportMode != Playing) return;
  if (skipCnt) { --skipCnt; if (skipCnt) return; }
  while (isNextByteAvailable()) {
    byte b = playBuf[playPos];
    // Never consume half a command or replay an already written frame.
    if (b != 0xFF && b != 0xFD) {
      byte available = (loadPos >= playPos) ? loadPos - playPos : bufSize - playPos + loadPos;
      if (available < 2) return;
    }
    b = getNextByte();
    if (b == 0xFF) return;
    if (b == 0xFD) { playbackFinished = true; return; }
    if (b == 0xFE) {
      skipCnt = uint16_t(getNextByte()) * 4;
      if (skipCnt) return;
    } else {
      byte value = getNextByte();
      if (b < 16) writeAY(b, value);
    }
  }
}

ISR(TIMER1_COMPA_vect) {
  sled.ShowStatus();
  playNotes();
  checkDemo();
  displayDue = true;
}

void displayOLED() {
  if (playbackFinished && transportMode == Playing) return;
  byte volumeA, volumeB, volumeC;
  uint16_t divA, divB, divC;
  uint32_t loaded;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    volumeA = ::volumeA; volumeB = ::volumeB; volumeC = ::volumeC;
    divA = ::divA; divB = ::divB; divC = ::divC;
    loaded = floaded;
  }
  if (transportMode == Playing && nVolMode != eVolFullScreen) {
    float fprc = (fsize > 0) ? 1000.0 * float(loaded) / float(fsize) : 0;
    int np = int(fprc + 0.5);
    if (np > 1000) np = 1000;
    sprintf(fperc, "%d.%d%%", np / 10, np % 10);
    oled.setCursor(0, 3);
    oled.print(fperc);
  }
  if (bSwitchMeter) {
    bSwitchMeter = false;
    nVolMode++;
    if (nVolMode >= eVolTotal)
      nVolMode = eVolChars;
    EEPROM.update(SAVE_VOL_MODE, nVolMode);
    if (nVolMode > eVolChars)
      oled.vreset();
    else
      showFile();
    if (eVolFullScreen == nVolMode)
      oled.clear();
    else {
      showName(true);
#ifdef FREQ_TWO_ROWS
      oled.clrMeter(eVolNotes == nVolMode);
#else
      oled.clrMeter(false);
#endif
    }
  }
  if (transportMode != Playing) {
    static byte blink = 0;
    if (++blink == 50) blink = 0;
    oled.setCursor(0, 3);
    if (blink < 25) oled.print(transportMode == Paused ? F("PAUSE") : F("STOP"));
    oled.clearToEOL();
    return;
  }
  if (eVolBars == nVolMode)
    oled.drawVol(volumeA, volumeB, volumeC);
  else if (eVolNotes == nVolMode || eVolFullScreen == nVolMode)
    oled.drawFreq2(eVolFullScreen == nVolMode, volumeA, volumeB, volumeC, divA, divB, divC);
  else {
    oled.clrMeter(false);
    oled.setCursor(32 + volumeA * 2 / 3, 3);
    oled.print(">");
    oled.setCursor(122 - (volumeC * 2 + 2) / 3, 3);
    oled.print("<");
    oled.setCursor(80 + volumeB * 2 / 3, 3);
    oled.print("]");
    oled.setCursor(75 - (volumeB * 2 + 2) / 3, 3);
    oled.print("[");
  }
}

void loop() {
  pollButtons();
  if (playbackFinished && (transportMode == Playing || fileNext)) {
    if (filesCount < 1) return;
    resetAY();
    const bool manual = fileNext != 0;
    const bool restart = restartRequested;
    restartRequested = false;
    const int32_t step = manual ? fileNext : 1;
    fileNext = 0;
    if (manual || (transportMode == Playing && !restart && !randMode)) {
      fileNum = (int32_t(fileNum) + step) % filesCount;
      if (fileNum < 0) fileNum += filesCount;
    }
    loadRandomFile(manual || restart || transportMode != Playing);
  }
  while (loadNextByte()) {}
  bool refresh;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { refresh = displayDue; displayDue = false; }
  if (refresh) displayOLED();
}

void out_595_word(byte lo, byte hi) {
  PDdigWL(OUT_SHIFT_LATCH_BIT);
  out_595_byte(hi);
  out_595_byte(lo);
  PDdigWH(OUT_SHIFT_LATCH_BIT);
}

void out_595_byte(byte x) {
  byte ii = 0b10000000;
  for (int i = 0; i < 8; i++) {
    if (ii & x)
      PEdigWH(OUT_SHIFT_DATA_BIT);
    else
      PEdigWL(OUT_SHIFT_DATA_BIT);
    ii >>= 1;
    PDdigWH(OUT_SHIFT_CLOCK_BIT);
    PDdigWL(OUT_SHIFT_CLOCK_BIT);
  }
}

byte in_165_byte() {
  byte byteR = 0;
  PBdigWL(IN_SHIFT_CLOCK_BIT);
  PBdigWL(IN_SHIFT_LATCH_BIT);
  delayMicroseconds(1);
  PBdigWH(IN_SHIFT_LATCH_BIT);
  delayMicroseconds(1);
  for (int i = 7; i >= 0; i--) { // read bit by bit 8 times
    if (bitRead(PINF, IN_SHIFT_DATA_BIT))
      bitSet(byteR, i); // read bit, insert it to byte
    PBdigWH(IN_SHIFT_CLOCK_BIT);
    delayMicroseconds(1);
    PBdigWL(IN_SHIFT_CLOCK_BIT);
    delayMicroseconds(1);
  }
  return byteR;
}

inline void PDdigWH(byte NB) {
  PORTD |= (1 << NB);
}
inline void PDdigWL(byte NB) {
  PORTD &= ~(1 << NB);
}
inline void PEdigWH(byte NB) {
  PORTE |= (1 << NB);
}
inline void PEdigWL(byte NB) {
  PORTE &= ~(1 << NB);
}
inline void PBdigWH(byte NB) {
  PORTB |= (1 << NB);
}
inline void PBdigWL(byte NB) {
  PORTB &= ~(1 << NB);
}
