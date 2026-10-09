#pragma once
#include <stdint.h>

// Active-low inputs: bits 0..5 navigate; bits 6/7 select modes.
class ButtonControls {
 public:
  struct Events { uint8_t navigation; uint8_t mode; };
  Events update(uint8_t raw, uint32_t now) {
    Events events = {0, 0};
    uint8_t pressed = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint8_t mask = uint8_t(1U << bit);
      if ((raw ^ raw_) & mask) changed_[bit] = now;
      if (((raw ^ stable_) & mask) && uint32_t(now - changed_[bit]) >= 20) {
        stable_ ^= mask;
        if (!(stable_ & mask)) pressed |= mask;
      }
    }
    raw_ = raw;
    events.navigation = pressed & 0x3f;
    const uint8_t held = uint8_t(~stable_) >> 6;
    const uint8_t newModes = pressed >> 6;
    if (!fired_ && newModes) {
      if (!pending_) started_ = now;
      pending_ |= newModes;
    }
    if (!fired_ && pending_ &&
        (pending_ == 3 || !held || uint32_t(now - started_) >= 100)) {
      events.mode = pending_;
      pending_ = 0;
      fired_ = true;
    }
    if (!held && !pending_) fired_ = false;
    return events;
  }
  uint8_t pressedMask() const { return uint8_t(~stable_); }
 private:
  uint8_t raw_ = 0xff, stable_ = 0xff, pending_ = 0;
  bool fired_ = false;
  uint32_t changed_[8] = {}, started_ = 0;
};
