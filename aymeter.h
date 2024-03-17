// Volume/note meter for Arduino Pro Micro PSG chiptune player

#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"

// comment this line for displaying note meter in one row
#define FREQ_TWO_ROWS
// comment this line for faster note falling in drawFreq2
#define SLOW_NOTE_FALL

const byte vol_char[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x08, 0x00, 0x00,
  0x00, 0x08, 0x00, 0x08, 0x00,
  0x00, 0x08, 0x08, 0x08, 0x00,
  0x00, 0x08, 0x14, 0x08, 0x00,
  0x00, 0x08, 0x1C, 0x08, 0x00,
  0x00, 0x1C, 0x14, 0x1C, 0x00,
  0x00, 0x1C, 0x1C, 0x1C, 0x00,
  0x00, 0x1C, 0x36, 0x1C, 0x00,
  0x00, 0x1C, 0x3E, 0x1C, 0x00,
  0x08, 0x1C, 0x3E, 0x1C, 0x08,
  0x08, 0x3E, 0x3E, 0x3E, 0x08,
  0x1C, 0x3E, 0x3E, 0x3E, 0x1C,
  0x1C, 0x3E, 0x7F, 0x3E, 0x1C,
  0x3E, 0x3E, 0x7F, 0x3E, 0x3E,
  0x3E, 0x7F, 0x7F, 0x7F, 0x3E,
  0x55, 0xAA, 0x55, 0xAA, 0x55,
};

const uint16_t note_div[] PROGMEM = {
  4095, //26.7 Hz, A-1
  3862, //28.3 Hz, A#-1
  3645, //30.0 Hz, B-1
  3441, //31.8 Hz, C0
  3247, //33.7 Hz, C#0
  3065, //35.7 Hz, D0
  2893, //37.8 Hz, D#0
  2731, //40.0 Hz, E0
  2577, //42.4 Hz, F0
  2433, //45.0 Hz, F#0
  2296, //47.6 Hz, G0
  2167, //50.5 Hz, G#0
  2046, //53.5 Hz, A0
  1931, //56.6 Hz, A#0
  1822, //60.0 Hz, B0
  1720, //63.6 Hz, C1
  1623, //67.4 Hz, C#1
  1532, //71.4 Hz, D1
  1446, //75.6 Hz, D#1
  1365, //80.1 Hz, E1
  1288, //84.9 Hz, F1
  1216, //89.9 Hz, F#1
  1148, //95.3 Hz, G1
  1083, //101.0 Hz, G#1
  1023, //106.9 Hz, A1
  965, //113.3 Hz, A#1
  911, //120.1 Hz, B1
  860, //127.2 Hz, C2
  811, //134.9 Hz, C#2
  766, //142.8 Hz, D2
  723, //151.3 Hz, D#2
  682, //160.4 Hz, E2
  644, //169.8 Hz, F2
  608, //179.9 Hz, F#2
  574, //190.5 Hz, G2
  541, //202.2 Hz, G#2
  511, //214.0 Hz, A2
  482, //226.9 Hz, A#2
  455, //240.4 Hz, B2
  430, //254.4 Hz, C3
  405, //270.1 Hz, C#3
  383, //285.6 Hz, D3
  361, //303.0 Hz, D#3
  341, //320.7 Hz, E3
  322, //339.7 Hz, F3
  304, //359.8 Hz, F#3
  287, //381.1 Hz, G3
  270, //405.1 Hz, G#3
  255, //428.9 Hz, A3
  241, //453.8 Hz, A#3
  227, //481.8 Hz, B3
  215, //508.7 Hz, C4
  202, //541.5 Hz, C#4
  191, //572.6 Hz, D4
  180, //607.6 Hz, D#4
  170, //643.4 Hz, E4
  161, //679.3 Hz, F4
  152, //719.6 Hz, F#4
  143, //764.9 Hz, G4
  135, //810.2 Hz, G#4
  127, //861.2 Hz, A4
  120, //911.5 Hz, A#4
  113, //967.9 Hz, B4
  107, //1022.2 Hz, C5
  101, //1082.9 Hz, C#5
  95, //1151.3 Hz, D5
  90, //1215.3 Hz, D#5
  85, //1286.8 Hz, E5
  80, //1367.2 Hz, F5
  76, //1439.1 Hz, F#5
  71, //1540.5 Hz, G5
  67, //1632.5 Hz, G#5
  63, //1736.1 Hz, A5
  60, //1822.9 Hz, A#5
  56, //1953.1 Hz, B5
  53, //2063.7 Hz, C6
  50, //2187.5 Hz, C#6
  47, //2327.1 Hz, D6
  45, //2430.6 Hz, D#6
  42, //2604.2 Hz, E6
  40, //2734.4 Hz, F6
  38, //2878.3 Hz, F#6
  35, //3125.0 Hz, G6
  33, //3314.4 Hz, G#6
  31, //3528.2 Hz, A6
  30, //3645.8 Hz, A#6
  28, //3906.2 Hz, B6
  26, //4206.7 Hz, C7
  25, //4375.0 Hz, C#7
  23, //4755.4 Hz, D7
  22, //4971.6 Hz, D#7
  21, //5208.3 Hz, E7
  20, //5468.8 Hz, F7
  19, //5756.6 Hz, F#7
  17, //6433.8 Hz, G7
  16, //6835.9 Hz, G#7
};

#define NOTE_COUNT 96
#define METER_LEFT 32
#define FS_LEFT 4
#define QUEUE_DEPTH 32

class SSD1306TextVol : public SSD1306AsciiAvrI2c {
  public:
    SSD1306TextVol(byte row) : SSD1306AsciiAvrI2c() {
      m_row = row; // 8-pixel height row index where volume indicator must be drawn
      vreset();
    }
    void clrMeter(bool bTwoLines) {
      clear(METER_LEFT, 127, bTwoLines ? 2 : 3, 3);
    }
    void vreset() {
      m_A = m_B = m_C = m_note[0] = m_note[1] = m_note[2] = -1;
      m_cnt = 0;
      for (byte i = 0; i < QUEUE_DEPTH; i++) {
        m_Qnote[i] = m_Qvol[i] = 0;
      }
      m_nQueueLen = m_nQueuePos = 0;
    }
    // draw volume bars
    void drawVol(byte volA, byte volB, byte volC) {
      drawMeter(volA, m_A, METER_LEFT);
      drawMeter(volB, m_B, METER_LEFT + 32);
      drawMeter(volC, m_C, METER_LEFT + 64);
    }
    // draw notes
    void drawFreq(bool bFullScreen, byte volA, byte volB, byte volC, uint16_t divA, uint16_t divB, uint16_t divC) {
      int8_t note[3];
      byte vol[3], i, cnt = getNotes(volA, volB, volC, divA, divB, divC, note, vol);
      if (cnt > 0) {
        byte val;
        if (m_cnt > 0) {
          for (byte j = 0; j < m_cnt; j++) {
            val = m_note[j];
            for (i = 0; i < cnt; i++) {
              if (val == note[i])
                break;
            }
            if (i >= cnt) // previous note not found - erase it
              drawZero(val, bFullScreen);
          }
        }
        for (i = 0; i < cnt; i++) {
          drawVal(note[i], vol[i], bFullScreen);
          m_note[i] = note[i];
        }
      }
      else if (m_cnt > 0) { // erase all previous notes
        for (i = 0; i < m_cnt; i++)
          drawZero(m_note[i], bFullScreen);
      }
      m_cnt = cnt;
    }
    // draw falling notes
    void drawFreq2(bool bFullScreen, byte volA, byte volB, byte volC, uint16_t divA, uint16_t divB, uint16_t divC) {
      int8_t note[3];
      byte vol[3], cnt = getNotes(volA, volB, volC, divA, divB, divC, note, vol),
                   i, j, k, cn, cv;
      if (cnt > 0) {
        for (i = 0; i < cnt; i++) {
          cn = note[i]; cv = vol[i];
          for (j = 0; j < m_nQueueLen; j++) {
            k = qIndex(j);
            if (cn == m_Qnote[k]) { // note found - check/update volume
              if (cv > m_Qvol[k]) {
                m_Qvol[k] = cv;
                drawVal(cn, cv, bFullScreen);
              }
              break;
            }
          }
          if (j >= m_nQueueLen) { // note not found - add it to queue
            if (QUEUE_DEPTH == m_nQueueLen && m_Qvol[m_nQueuePos] > 0) {
              // erase oldest element in queue from screen
              drawZero(m_Qnote[m_nQueuePos], bFullScreen);
            }
            m_Qnote[m_nQueuePos] = cn;
            m_Qvol[m_nQueuePos] = cv;
            drawVal(cn, cv, bFullScreen);
            if (m_nQueuePos > 0)
              m_nQueuePos--;
            else
              m_nQueuePos = QUEUE_DEPTH - 1;
            if (m_nQueueLen < QUEUE_DEPTH)
              m_nQueueLen++;
          }
        }
      }
      if (m_nQueueLen > 0) {
        for (j = 0; j < m_nQueueLen; j++) {
          k = qIndex(j);
          cn = m_Qnote[k];
          if (cnt > 0) {
            for (i = 0; i < cnt; i++) {
              if (cn == note[i])
                break;
            }
            if (i < cnt) // note already processed, skip it
              continue;
          }
          cv = m_Qvol[k];
          if (cv > 1) {
#ifdef SLOW_NOTE_FALL
            cv--;
#else
            cv >>= 1;
#endif
            m_Qvol[k] = cv;
            drawVal(cn, cv, bFullScreen);
          }
          else if (1 == cv) {
            m_Qvol[k] = 0;
            drawZero(cn, bFullScreen);
          }
        }
        int8_t notzero = -1;
        for (j = 0; j < m_nQueueLen; j++) {
          k = qIndex(j);
          cv = m_Qvol[k];
          if (cv > 0)
            notzero = j;
        }
        m_nQueueLen = notzero + 1;
      }
    }
  protected:
    void drawMeter(byte vol, int8_t& _vol, byte pos) {
      if (vol > 15) vol = 15;
      if (vol == _vol) return;
      byte nv = vol + 1, i; bool bErase = false;
      if (_vol < 0)
        i = 0;
      else if (vol > _vol)
        i = _vol + 1;
      else if (vol < _vol) {
        nv = _vol + 1;
        i = vol + 1;
        bErase = true;
      }
      if (_vol > -1)
        pos += i * 2;
      for (; i < nv; i++, pos += 2) {
        setCursor(pos, m_row);
        if (bErase)
          ssd1306WriteRam(0);
        else
          ssd1306WriteRam(0xFF ^ (0x1F >> ((i + 1) / 4)));
      }
      _vol = vol;
    }
    byte getClosest(uint16_t nv1, byte iv1, uint16_t nv2, byte iv2, uint16_t target) const
    {
      if (target - nv2 > nv1 - target)
        return iv1;
      else
        return iv2;
    }
    int8_t nearestNote(uint16_t target) const
    {
      if (target < 1) return -1;
      const byte n = NOTE_COUNT;
      uint16_t vmid = pgm_read_word(&note_div[0]);
      if (target >= vmid)
        return 0;
      vmid = pgm_read_word(&note_div[n - 1]);
      if (target <= vmid)
        return n - 1;
      byte i = 0, j = n, mid = 0;
      uint16_t vmid1;
      while (i < j) {
        mid = (i + j) / 2;
        vmid = pgm_read_word(&note_div[mid]);
        if (vmid == target)
          return mid;
        if (target > vmid) {
          vmid1 = pgm_read_word(&note_div[mid - 1]);
          if (mid > 0 && target < vmid1)
            return getClosest(vmid1, mid - 1, vmid, mid, target);
          j = mid;
        }
        else {
          vmid1 = pgm_read_word(&note_div[mid + 1]);
          if (mid < n - 1 && target > vmid1)
            return getClosest(vmid, mid, vmid1, mid + 1, target);
          i = mid + 1;
        }
      }
      return mid;
    }
    byte getNotes(byte volA, byte volB, byte volC, uint16_t divA, uint16_t divB, uint16_t divC, int8_t* note, byte* vol) const
    {
      int8_t noteA = (volA > 0) ? nearestNote(divA) : -1,
             noteB = (volB > 0) ? nearestNote(divB) : -1,
             noteC = (volC > 0) ? nearestNote(divC) : -1;
      byte cnt = 0;
      if (noteA > -1) {
        note[cnt] = noteA;
        vol[cnt] = volA;
        if (noteA == noteB) {
          vol[cnt] = max(vol[cnt], volB);
          noteB = -1;
        }
        if (noteA == noteC) {
          vol[cnt] = max(vol[cnt], volC);
          noteC = -1;
        }
        cnt++;
      }
      if (noteB > -1) {
        note[cnt] = noteB;
        vol[cnt] = volB;
        if (noteB == noteC) {
          vol[cnt] = max(vol[cnt], volC);
          noteC = -1;
        }
        cnt++;
      }
      if (noteC > -1) {
        note[cnt] = noteC;
        vol[cnt] = volC;
        cnt++;
      }
      return cnt;
    }
    void drawVal(byte note, byte vol, bool bFullScreen) {
      byte val = vol;
      if (val > 16) val = 16;
      if (bFullScreen) {
        val *= 5;
        setCursor(FS_LEFT + (note % 24) * 5, note / 24);
        for (byte i = 0; i < 5; i++) {
          ssd1306WriteRamBuf(pgm_read_byte(&vol_char[val + i]));
          m_col++;
        }
        setCol(m_col);
        return;
      }
#ifdef FREQ_TWO_ROWS
      setCursor(METER_LEFT + note, m_row - 1);
      if (val > 8) {
        ssd1306WriteRam(0xFF ^ (0xFF >> (val - 8)));
        setCursor(METER_LEFT + note, m_row);
        ssd1306WriteRam(0xFF);
      }
      else {
        ssd1306WriteRam(0);
        setCursor(METER_LEFT + note, m_row);
        ssd1306WriteRam(0xFF ^ (0xFF >> val));
      }
#else // !FREQ_TWO_ROWS
      setCursor(METER_LEFT + note, m_row);
      if (val > 1) val >>= 1;
      if (val > 8) val = 8;
      ssd1306WriteRam(0xFF ^ (0xFF >> val));
#endif // FREQ_TWO_ROWS
    }
    void drawZero(byte note, bool bFullScreen) {
      if (bFullScreen) {
        setCursor(FS_LEFT + (note % 24) * 5, note / 24);
        for (byte i = 0; i < 5; i++) {
          ssd1306WriteRamBuf(0);
          m_col++;
        }
        setCol(m_col);
        return;
      }
#ifdef FREQ_TWO_ROWS
      setCursor(METER_LEFT + note, m_row - 1);
      ssd1306WriteRam(0);
#endif // FREQ_TWO_ROWS
      setCursor(METER_LEFT + note, m_row);
      ssd1306WriteRam(0);
    }
    byte qIndex(byte i) const {
      return (m_nQueuePos + i + 1) % QUEUE_DEPTH;
    }
  private:
    byte m_row, m_cnt;
    int8_t m_A, m_B, m_C, m_note[3];
    byte m_Qnote[QUEUE_DEPTH], m_Qvol[QUEUE_DEPTH];
    byte m_nQueueLen, m_nQueuePos;
};
