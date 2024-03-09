// Arduino Pro Micro project: chiptune player of PSG files from SD card
// on AY-3-8910 chip with OLED display, SRAM for storing file names
// and 8 buttons for switching between modes and songs

#include <EEPROM.h>
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
static const uint32_t nMaxAdr = 0x20000, nMaxFiles = 0xFFFF;
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

static const byte IN_SHIFT_LATCH_PIN = 8; // -> pin 1 of 74HC165 - data output latch (ST_CP)
static const byte IN_SHIFT_CLOCK_PIN = 9; // -> pin 2 of 74HC165 - clock pin SCK (SH_CP)
static const byte IN_SHIFT_DATA_PIN = A0; // -> pin 9 of 74HC165 - data input, slave in SI (DS)

// bits of B/F ports, not pin numbers!
static const byte IN_SHIFT_DATA_BIT = 7;  // pin A0 (PF7) => DS pin 9
static const byte IN_SHIFT_LATCH_BIT = 4; // pin 8 (PB4) => ST_CP pin 1
static const byte IN_SHIFT_CLOCK_BIT = 5; // pin 9 (PB5) => SH_CP pin 2

void setupShiftIn() {
  pinMode(IN_SHIFT_DATA_PIN, INPUT);
  pinMode(IN_SHIFT_CLOCK_PIN, OUTPUT);
  pinMode(IN_SHIFT_LATCH_PIN, OUTPUT);
  digitalWrite(IN_SHIFT_LATCH_PIN, HIGH);
  digitalWrite(IN_SHIFT_CLOCK_PIN, LOW);
}

#define BTN_TIMEOUT_MS 200

class CBtn {
    CBtn() {}
  public:
    CBtn(byte nBit, byte* pbIn) {
      m_bPressed = false;
      m_lastTime = 0;
      m_nBit = nBit;
      m_pbIn = pbIn;
    }
    bool Pressed() {
      bool bPressed = !bitRead(*m_pbIn, m_nBit), ret = false;
      if (bPressed && !m_bPressed) {
        long nCurTime = millis();
        if (nCurTime - m_lastTime > BTN_TIMEOUT_MS) {
          ret = true;
          m_lastTime = nCurTime;
        }
      }
      m_bPressed = bPressed;
      return ret;
    }
  private:
    bool m_bPressed;
    unsigned long m_lastTime;
    byte m_nBit;
    byte* m_pbIn;
};

#define BTN_BOTH_MS 100
#define BTN1_MASK 0x01
#define BTN2_MASK 0x02
#define BTN12_MASK 0x03

class CBtn2 {
    CBtn2() {}
  public:
    CBtn2(byte nBit1, byte nBit2, byte* pbIn) {
      m_nBit1 = nBit1; m_nBit2 = nBit2;
      m_pbIn = pbIn;
      m_press = m_ret = 0;
      m_nPressTime1 = m_nPressTime2 = 0;
    }
    void CheckPress() {
      const bool bPress1 = !bitRead(*m_pbIn, m_nBit1), bPress2 = !bitRead(*m_pbIn, m_nBit2);
      const long nCurTime = millis();
      if (bPress1 && 0 == (m_press & BTN1_MASK) && nCurTime - m_nPressTime1 > BTN_TIMEOUT_MS) {
        m_nPressTime1 = nCurTime;
        m_press |= BTN1_MASK;
      }
      else if (!bPress1 && 0 != (m_press & BTN1_MASK))
        m_press &= !BTN1_MASK;
      if (bPress2 && 0 == (m_press & BTN2_MASK) && nCurTime - m_nPressTime2 > BTN_TIMEOUT_MS) {
        m_nPressTime2 = nCurTime;
        m_press |= BTN2_MASK;
      }
      else if (!bPress2 && 0 != (m_press & BTN2_MASK))
        m_press &= !BTN2_MASK;
      if (m_ret != 0 && 0 == (m_press & BTN12_MASK))
        m_ret = 0;
    }
    byte Pressed() {
      byte ret = 0;
      if (0 == m_ret && 0 != (m_press & BTN12_MASK)) {
        if (BTN12_MASK == (m_press & BTN12_MASK))
          ret = BTN12_MASK;
        else {
          const long nCurTime = millis();
          if (0 != (m_press & BTN1_MASK) && 0 == (m_press & BTN2_MASK)) {
            if (nCurTime - m_nPressTime1 >= BTN_BOTH_MS)
              ret = BTN1_MASK;
          }
          else if (0 == (m_press & BTN1_MASK) && 0 != (m_press & BTN2_MASK)) {
            if (nCurTime - m_nPressTime2 >= BTN_BOTH_MS)
              ret = BTN2_MASK;
          }
        }
        m_ret = ret;
      }
      return ret;
    }
  private:
    byte* m_pbIn;
    byte m_nBit1, m_nBit2, m_press, m_ret;
    long m_nPressTime1, m_nPressTime2;
};

byte inBtn = 255, outLo = 0, outHi = 0;

CBtn btn1(0, &inBtn), btn2(1, &inBtn), btn3(2, &inBtn), btn4(3, &inBtn),
     btn5(4, &inBtn), btn6(5, &inBtn);
CBtn2 btn78(6, 7, &inBtn);

void dispMsg(const char* pstr) {
  oled.setCursor(0, 0);
  oled.print(pstr);
}

const char *supportedFileExt = ".psg";
static const byte fileExtLen = strlen(supportedFileExt);
static const int nTxtLen = 40;
char fname[256], fperc[8], fmode[22];
uint32_t fsize = 0, fnlen, floaded;

// structure:
// { uint24 offset/length, uint16 random number }
static const byte szN = 3, szR = sizeof(uint16_t),
                  szStruct = szN + szR;
SdFat sd;

class CFileList {
    uint16_t m_nFiles, m_nRandom;
    uint32_t m_nFreeAdrLo, m_nFreeAdrHi;
    bool checkFile(SdFile entry) {
      if (m_nFiles >= nMaxFiles) return false;
      entry.getName(fname, 255);
      fsize = entry.fileSize();
      fnlen = strlen(fname);
      bool bOK = !entry.isHidden() && !entry.isDir() && (fnlen > fileExtLen &&
                 !strcasecmp(fname + fnlen - fileExtLen, supportedFileExt) && fsize > 0);
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
    void FillRandom(uint16_t nNotFirst = 0)
    {
      const uint16_t n = m_nFiles;
      if (n < 2) return;
      uint16_t i, j, t, v;
      uint32_t adr = nMaxAdr - szR, adr1;
      for (i = 0; i < n; i++) {
        sram.writeBlock(adr, szR, (byte*)&i);
        adr -= szStruct;
      }
      for (i = 0; i < n; i++) {
        adr = nMaxAdr - szR - i * szStruct;
        sram.readBlock(adr, szR, (byte*)&t); // t = a[i]
        j = random(n) % (n - i) + i;
        adr1 = nMaxAdr - szR - j * szStruct;
        sram.readBlock(adr1, szR, (byte*)&v); // v = a[j]
        sram.writeBlock(adr, szR, (byte*)&v); // a[i] = a[j]
        sram.writeBlock(adr1, szR, (byte*)&t); // a[j] = t
      }
      adr = nMaxAdr - szR;
      sram.readBlock(adr1, szR, (byte*)&v);
      if (v == nNotFirst) { // swap first and last elements
        adr1 = nMaxAdr - szR - (m_nFiles - 1) * szStruct;
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
    void GetList() {
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
      if (index < 0 || index >= m_nFiles)
        return;
      uint32_t adr = nMaxAdr - (index + 1) * szStruct, offset = 0;
      fsize = fnlen = 0;
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
      sram.readBlock(offset, fnlen, (byte*)fname);
      fname[fnlen] = 0;
      strcat(fname, supportedFileExt);
    }
    uint16_t getRandom() {
      uint16_t rnd = 0;
      if (m_nFiles > 1) {
        uint32_t adr = nMaxAdr - szR - szStruct * m_nRandom;
        if (m_nRandom >= m_nFiles) {
          adr = nMaxAdr - szR - szStruct * (m_nFiles - 1);
          sram.readBlock(adr, szR, (byte*)&rnd);
          FillRandom(rnd);
        }
        adr = nMaxAdr - szR - szStruct * m_nRandom;
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
  eVolTotal,
};
static const bool bFreqTwoRows = true; // false = display note/frequency indicator in one line

static const int bufSize = 300;
bool demoMode = false, randMode = true, bSwitchMeter = false;
byte nVolMode = eVolChars;
static const int demoLen = 10000, demoFadeLen = 500;
SdFile fp;
byte volumeA, volumeB, volumeC;
uint16_t divA, divB, divC;
byte playBuf[bufSize];
bool playbackFinished = true;
bool playbackSkip;
int filesCount;
int fileNum, fileNext = 0;
int loadPos;
int playPos;
int skipCnt;
int totalPos;
float globalVolume;

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
  OCR1A = 1250;
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
  generateSeed();
  setupTimer();
  fg.set(1780000); // set Timer4 for AY clock, pin #5 output mode is set inside
  resetAY();
  delay(500);
  sram.begin();
  initSD();
  sled.SetOK(filesCount > 0);
  loadRandomFile();
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
  for (int i = 0; i < noOfBits; ++i)
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
  globalVolume = 1;

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

void setVolume(float volume) {
  globalVolume = volume;
  writeAY(8, volumeA);
  writeAY(9, volumeB);
  writeAY(10, volumeC);
}

void writeAY(const byte port, byte data) { // write data to AY register
  if (port == 8 || port == 9 || port == 10) {
    if (port == 8) volumeA = data;
    if (port == 9) volumeB = data;
    if (port == 10) volumeC = data;
    data = (byte)(data * globalVolume);
  }
  if (port == 0)
    *((byte*)&divA) = data;
  else if (port == 1)
    *((byte*)&divA + 1) = data & 0xF;
  else if (port == 2)
    *((byte*)&divB) = data;
  else if (port == 3)
    *((byte*)&divB + 1) = data & 0xF;
  else if (port == 4)
    *((byte*)&divC) = data;
  else if (port == 5)
    *((byte*)&divC + 1) = data & 0xF;

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
  loadDirectory();
}

void loadDirectory() {
  files.GetList();
  filesCount = files.getCount();
}

void loadRandomFile() {
  if (randMode)
    fileNum = files.getRandom();
  files.getFile(fileNum);
  SdFile file;
  file.open(fname, O_READ);
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
  char buf[6]; sprintf(buf, "%d", fileNum + 1);
  if (!bFreqTwoRows || (bFreqTwoRows && nVolMode < eVolNotes )) {
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
  if (eVolNotes == nVolMode && bFreqTwoRows) {
    oled.print(buf);
    oled.print("    ");
  }
}

void showFile() {
  oled.clear();
  showMode(false);
  showName(false);
  if (nVolMode > eVolChars)
    oled.vreset();
}

void playFile(SdFile entry) {
  fp.close();
  fp = entry;
  loadPos = playPos = totalPos = floaded = 0;
  showFile();
  while (fp.available()) {
    byte b = fp.read();
    if (b == 0xFF) break;
  }
  playbackFinished = false;
}

bool loadNextByte() {
  if (loadPos == playPos - 1 || loadPos == bufSize - 1 && playPos == 0)
    return false;
  byte b = fp.available() ? fp.read() : 0xFD;
  playBuf[loadPos++] = b;
  if (loadPos == bufSize) loadPos = 0;
  floaded++;
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
  if (demoMode && totalPos >= demoLen && !playbackFinished) {
    int demoPos = totalPos - demoLen;
    setVolume(1.0 - demoPos / (float)demoFadeLen);
    if (demoPos >= demoFadeLen) playbackFinished = true;
  }
}

void playNotes() {
  inBtn = in_165_byte();
  btn78.CheckPress();
  const byte n78 = btn78.Pressed();
  if (BTN12_MASK == n78) // both buttons 7 and 8 pressed - switch volume indicator mode
    bSwitchMeter = true;
  else if (BTN2_MASK == n78) { // only button 8 pressed - switch demo mode
    demoMode = !demoMode;
    showMode(true);
    EEPROM.write(SAVE_DEMO_MODE, demoMode ? 1 : 0);
  }
  else if (BTN1_MASK == n78) { // only button 7 pressed - switch random/seq mode
    randMode = !randMode;
    showMode(true);
    EEPROM.write(SAVE_RAND_MODE, randMode ? 1 : 0);
  }
  bool bNextPressed = false;
  if (btn1.Pressed()) {
    fileNext = -1;
    bNextPressed = true;
  }
  if (btn2.Pressed()) {
    fileNext = 1;
    bNextPressed = true;
  }
  if (btn3.Pressed()) {
    fileNext = -5;
    bNextPressed = true;
  }
  if (btn4.Pressed()) {
    fileNext = 5;
    bNextPressed = true;
  }
  if (btn5.Pressed()) {
    fileNext = -10;
    bNextPressed = true;
  }
  if (btn6.Pressed()) {
    fileNext = 10;
    bNextPressed = true;
  }
  if (bNextPressed)
    playbackSkip = true;
  else if (playbackSkip) {
    playbackSkip = false;
    playbackFinished = true;
  }
  if (playbackFinished || --skipCnt > 0)
    return;
  int oldPlayPos = playPos;
  int oldTotalPos = totalPos;
  while (isNextByteAvailable()) {
    byte b = getNextByte();
    if (b == 0xFF) {
      break;
    } else if (b == 0xFD) {
      playbackFinished = true;
      break;
    } else if (b == 0xFE) {
      if (isNextByteAvailable()) {
        skipCnt = getNextByte();
        skipCnt *= 4;
        break;
      }
    } else if (b <= 0xFC) {
      if (isNextByteAvailable()) {
        byte v = getNextByte();
        if (b < 16) writeAY(b, v);
      }
    }
  }
  if (!isNextByteAvailable()) {
    playPos = oldPlayPos;
    totalPos = oldTotalPos;
  }
}

ISR(TIMER1_COMPA_vect) {
  sled.ShowStatus();
  playNotes();
  displayOLED();
  checkDemo();
}

void displayOLED() {
  if (playbackFinished) return;
  float fprc = (fsize > 0) ? 1000.0 * float(floaded) / float(fsize) : 0;
  int np = int(fprc + 0.5);
  if (np > 1000) np = 1000;
  sprintf(fperc, "%d.%d%%", np / 10, np % 10);
  oled.setCursor(0, 3);
  oled.print(fperc);
  if (bSwitchMeter) {
    bSwitchMeter = false;
    nVolMode++;
    if (nVolMode >= eVolTotal)
      nVolMode = eVolChars;
    EEPROM.write(SAVE_VOL_MODE, nVolMode);
    if (nVolMode > eVolChars)
      oled.vreset();
    showName(true);
    oled.clrMeter(eVolNotes == nVolMode && bFreqTwoRows);
  }
  if (eVolBars == nVolMode)
    oled.drawVol(volumeA, volumeB, volumeC);
  else if (eVolNotes == nVolMode)
    oled.drawFreq2(bFreqTwoRows, volumeA, volumeB, volumeC, divA, divB, divC);
  else {
    oled.clrMeter(eVolNotes == nVolMode && bFreqTwoRows);
    oled.setCursor(32 + volumeA / 1.5, 3);
    oled.print(">");
    oled.setCursor((122 - volumeC / 1.5), 3);
    oled.print("<");
    oled.setCursor((80 + volumeB / 1.5), 3);
    oled.print("]");
    oled.setCursor((75 - volumeB / 1.5), 3);
    oled.print("[");
  }
}

void loop() {
  loadNextByte();
  if (playbackFinished) {
    resetAY();
    if (fileNext != 0) {
      fileNum += fileNext;
      fileNext = 0;
      if (fileNum < 0) {
        fileNum += filesCount;
        if (fileNum < 0)
          fileNum = 0;
      }
      else if (fileNum >= filesCount) {
        fileNum -= filesCount;
        if (fileNum >= filesCount)
          fileNum = filesCount - 1;
      }
    }
    else {
      fileNum++;
      if (fileNum >= filesCount)
        fileNum = 0;
    }
    loadRandomFile();
  }
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
  PBdigWL(IN_SHIFT_LATCH_BIT);
  PBdigWH(IN_SHIFT_LATCH_BIT);
  for (int i = 7; i >= 0; i--) { // read bit by bit 8 times
    if (bitRead(PINF, IN_SHIFT_DATA_BIT))
      bitSet(byteR, i); // read bit, insert it to byte
    PBdigWH(IN_SHIFT_CLOCK_BIT);
    PBdigWL(IN_SHIFT_CLOCK_BIT);
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
