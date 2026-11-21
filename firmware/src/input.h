// EC11 knob (quadrature via interrupts) + its push, and the module's K0 button.
// Buttons are debounced, active low, with click and long-press events.
#pragma once
#include <Arduino.h>
#include "soc/gpio_reg.h"
#include "board.h"

class Input {
 public:
  enum class Btn : uint8_t { None, KnobClick, KnobLong, KeyClick, KeyLong };
  static constexpr uint32_t LONG_MS = 600;

  void begin() {
    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_SW, INPUT_PULLUP);
    pinMode(KEY0, INPUT_PULLUP);
    self_ = this;
    state_ = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
    attachInterrupt(ENC_A, isr, CHANGE);
    attachInterrupt(ENC_B, isr, CHANGE);
  }

  // Detents turned since the last call (+ = clockwise, before ENC_DIR).
  int takeDetents() {
    noInterrupts();
    int32_t steps = steps_;
    interrupts();
    int32_t det = steps / 4;  // this EC11: 4 transitions per detent
    int d = det - lastDet_ + injDet_;
    lastDet_ = det;
    injDet_ = 0;
    return d;
  }

  // Simulated input from the serial console (screenshots, testing).
  void inject(int detents) { injDet_ += detents; }
  void inject(Btn b) { injBtn_ = b; }

  Btn pollButtons() {
    if (injBtn_ != Btn::None) {
      Btn b = injBtn_;
      injBtn_ = Btn::None;
      return b;
    }
    switch (scan(ENC_SW, knob_)) {
      case Edge::Click: return Btn::KnobClick;
      case Edge::Long: return Btn::KnobLong;
      default: break;
    }
    switch (scan(KEY0, key_)) {
      case Edge::Click: return Btn::KeyClick;
      case Edge::Long: return Btn::KeyLong;
      default: break;
    }
    return Btn::None;
  }

 private:
  enum class Edge : uint8_t { None, Click, Long };
  struct Button {
    bool last = true;  // stable level, pulled up = released
    uint32_t changed = 0, pressedAt = 0;
    bool longSent = false;
  };

  static Input* self_;
  volatile int32_t steps_ = 0;
  volatile uint8_t state_ = 0;
  int32_t lastDet_ = 0;
  int injDet_ = 0;
  Btn injBtn_ = Btn::None;
  Button knob_, key_;

  // Click fires on release; Long fires once while held and eats the release.
  Edge scan(int pin, Button& b) {
    bool level = digitalRead(pin);
    uint32_t t = millis();
    if (level != b.last) {
      if (t - b.changed < 25) return Edge::None;  // bounce window
      b.changed = t;
      b.last = level;
      if (!level) {
        b.pressedAt = t;
        b.longSent = false;
        return Edge::None;
      }
      return b.longSent ? Edge::None : Edge::Click;
    }
    if (!b.last && !b.longSent && t - b.pressedAt > LONG_MS) {
      b.longSent = true;
      return Edge::Long;
    }
    return Edge::None;
  }

  static inline int IRAM_ATTR rd(int pin) { return (REG_READ(GPIO_IN_REG) >> pin) & 0x1; }
  static void IRAM_ATTR isr();
};
inline Input* Input::self_ = nullptr;

static const int8_t ENC_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
void IRAM_ATTR Input::isr() {
  Input* s = self_;
  uint8_t st = ((s->state_ << 2) | (rd(ENC_A) << 1) | rd(ENC_B)) & 0x0F;
  s->state_ = st;
  s->steps_ += ENC_TABLE[st];
}
