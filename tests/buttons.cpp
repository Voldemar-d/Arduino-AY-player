#include "../ButtonControls.h"

extern "C" {
volatile uint8_t test_done = 0;
volatile uint8_t test_failure = 0;
volatile uint8_t test_count = 0;
}

static void expect(bool condition) {
  ++test_count;
  if (!condition && !test_failure) test_failure = test_count;
}

int main() {
  for (uint8_t bit = 0; bit < 6; ++bit) {
    ButtonControls b;
    const uint8_t raw = 0xff ^ (1 << bit);
    expect(b.update(raw, 0).navigation == 0);
    expect(b.update(raw, 19).navigation == 0);
    expect(b.update(raw, 20).navigation == (1 << bit));
    expect(b.update(raw, 1000).navigation == 0);
    b.update(255, 1005); b.update(255, 1025);
    b.update(raw, 1030);
    expect(b.update(raw, 1050).navigation == (1 << bit));
  }
  for (uint8_t mode = 1; mode <= 2; ++mode) {
    ButtonControls b;
    const uint8_t raw = 255 ^ (mode << 6);
    b.update(raw, 0);
    expect(b.update(raw, 20).mode == 0);
    expect(b.update(raw, 119).mode == 0);
    expect(b.update(raw, 120).mode == mode);
    expect(b.update(raw, 1000).mode == 0);
    b.update(255, 1005); b.update(255, 1025);
    b.update(raw, 1030); b.update(raw, 1050);
    expect(b.update(raw, 1150).mode == mode);
  }
  for (uint8_t first = 1; first <= 2; ++first) {
    ButtonControls b;
    uint8_t raw = 255 ^ (first << 6);
    b.update(raw, 0); b.update(raw, 20);
    b.update(63, 50);
    expect(b.update(63, 70).mode == 3);
    expect(b.update(63, 500).mode == 0);
    b.update(raw, 510);
    expect(b.update(raw, 530).mode == 0);
    b.update(255, 540); b.update(255, 560);
    b.update(raw, 570); b.update(raw, 590);
    expect(b.update(raw, 690).mode == first);
  }
  {
    ButtonControls b;
    b.update(191, 0); b.update(191, 20); b.update(255, 40);
    expect(b.update(255, 60).mode == 1); // short, debounced tap
    expect(b.update(255, 200).mode == 0);
  }
  {
    ButtonControls b;
    b.update(254, 0); b.update(255, 5); b.update(254, 10);
    b.update(255, 15); b.update(254, 20);
    expect(b.update(254, 39).navigation == 0);
    expect(b.update(254, 40).navigation == 1);
    expect(b.update(254, 60).navigation == 0);
  }
  {
    ButtonControls b;
    b.update(254, 0xfffffff0UL);
    expect(b.update(254, 3).navigation == 0);
    expect(b.update(254, 4).navigation == 1);
  }
  {
    ButtonControls b;
    b.update(63, 0);
    expect(b.update(63, 20).mode == 3);
    expect(b.update(63, 120).mode == 0);
  }
  test_done = 0xa5;
  while (true) {}
}
