// Retro drawing layer (SNES dark), the claude-usage-display primitives on a
// banded framebuffer: each frame is drawn into a 320x40 RGB565 canvas six
// times (one per band) and pushed to the panel, so nothing flickers and the
// C3 only needs 25 KB instead of a 150 KB full-screen buffer.
#pragma once
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "board.h"
#include "pixelfont.h"
#include "theme.h"

enum Font : uint8_t { FP, FG };  // FP: Press Start 2P 8x8, FG: GLCD 6x8

static inline int adv(Font f, int s) { return (f == FP ? 8 : 6) * s; }
static inline int tw(const char* str, Font f = FP, int s = 1) { return strlen(str) * adv(f, s); }

class Gfx {
 public:
  static constexpr int BAND_H = 40;

  explicit Gfx(Adafruit_ST7789& tft) : tft_(tft) {}

  bool begin() {
    canvas_ = new GFXcanvas16(SCR_W, BAND_H);
    return canvas_ && canvas_->getBuffer();
  }

  // When set, each band is also written to Serial as raw RGB565 (little
  // endian), top to bottom: the serial console's screenshot.
  bool capture = false;

  // Calls draw() once per band; primitives clip to the current band.
  template <typename F>
  void frame(F&& draw) {
    for (oy_ = 0; oy_ < SCR_H; oy_ += BAND_H) {
      canvas_->fillScreen(C::bg);
      draw();
      tft_.drawRGBBitmap(0, oy_, canvas_->getBuffer(), SCR_W, BAND_H);
      if (capture) Serial.write((const uint8_t*)canvas_->getBuffer(), SCR_W * BAND_H * 2);
    }
  }

  // ---- primitives ----
  bool inBand(int y, int h) const { return y < oy_ + BAND_H && y + h > oy_; }

  void rect(int x, int y, int w, int h, uint16_t c) {
    if (w <= 0 || h <= 0 || !inBand(y, h)) return;
    canvas_->fillRect(x, y - oy_, w, h, c);
  }

  // Rounded rectangle with 2px pixel-art corners.
  void rrect(int x, int y, int w, int h, uint16_t c) {
    rect(x + 2, y, w - 4, h, c);
    rect(x, y + 2, w, h - 4, c);
    rect(x + 1, y + 1, w - 2, h - 2, c);
  }

  void disc(int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; dy++) {
      int dx = (int)floorf(sqrtf(r * r + r * 0.8f - dy * dy));
      rect(cx - dx, cy + dy, dx * 2 + 1, 1, c);
    }
  }

  // Small up/down arrow, 9 wide x 5 tall, centered on cx.
  void triangle(int cx, int cy, bool up, uint16_t c) {
    for (int i = 0; i < 5; i++) rect(cx - i, up ? cy + i : cy + 4 - i, i * 2 + 1, 1, c);
  }

  // Transport icons, 8x7 at (x, y).
  void playIcon(int x, int y, uint16_t c) {
    for (int r = 0; r < 7; r++) rect(x, y + r, 2 * (1 + (r <= 3 ? r : 6 - r)), 1, c);
  }
  void pauseIcon(int x, int y, uint16_t c) {
    rect(x, y, 3, 7, c);
    rect(x + 5, y, 3, 7, c);
  }
  void stopIcon(int x, int y, uint16_t c) { rect(x, y, 7, 7, c); }

  // ---- text ----
  void text(const char* str, int x, int y, Font f, int s, uint16_t c) {
    if (!inBand(y, 8 * s)) return;
    for (const char* p = str; *p; p++, x += adv(f, s)) {
      uint8_t code = (uint8_t)*p;
      if (code < PF_FIRST || code > PF_LAST) code = '?';
      if (f == FP) {
        const uint8_t* g = PS2P_FONT[code - PF_FIRST];
        for (int r = 0; r < 8; r++) {
          uint8_t b = pgm_read_byte(g + r);
          for (int k = 0; k < 8;) {  // each horizontal run as one rect
            if (!(b & (0x80 >> k))) { k++; continue; }
            int start = k;
            while (k < 8 && (b & (0x80 >> k))) k++;
            rect(x + start * s, y + r * s, (k - start) * s, s, c);
          }
        }
      } else {
        const uint8_t* g = GLCD_FONT[code - PF_FIRST];
        for (int k = 0; k < 5; k++) {
          uint8_t b = pgm_read_byte(g + k);
          for (int r = 0; r < 8;) {  // vertical runs
            if (!(b & (1 << r))) { r++; continue; }
            int start = r;
            while (r < 8 && (b & (1 << r))) r++;
            rect(x + k * s, y + start * s, s, (r - start) * s, c);
          }
        }
      }
    }
  }
  void center(const char* str, int cx, int y, Font f, int s, uint16_t c) {
    text(str, cx - tw(str, f, s) / 2, y, f, s, c);
  }
  void rtext(const char* str, int xr, int y, Font f, int s, uint16_t c) {
    text(str, xr - tw(str, f, s), y, f, s, c);
  }

  // Cuts str with a trailing '~' so it fits maxW pixels. Returns one of four
  // rotating buffers, so a few calls can share one expression.
  static const char* fit(const char* str, int maxW, Font f = FP, int s = 1) {
    static char bufs[4][100];
    static uint8_t slot = 0;
    char* out = bufs[slot++ & 3];
    int n = maxW / adv(f, s);
    if (n > 99) n = 99;
    int len = strlen(str);
    if (len <= n) { memcpy(out, str, len + 1); return out; }
    if (n <= 0) { out[0] = 0; return out; }
    memcpy(out, str, n - 1);
    out[n - 1] = '~';
    out[n] = 0;
    return out;
  }

  // ---- motifs ----
  // Framed card: key2 border, face, 1px light highlight on top.
  void card(int x, int y, int w, int h, uint16_t face = C::panel) {
    rect(x, y, w, h, C::key2);
    rect(x + 1, y + 1, w - 2, h - 2, face);
    rect(x + 1, y + 1, w - 2, 1, C::white);
  }

  // Keycap: face plus a 2px side below. Main label in Press Start 2P, and an
  // optional GLCD caption under it naming the physical control.
  void key(int x, int y, int w, int h, uint16_t body, const char* label, const char* sub,
           uint16_t ink = C::keyink, uint16_t subInk = C::dim) {
    rrect(x, y + 2, w, h - 2, C::key2);
    rrect(x, y, w, h - 2, body);
    if (sub && *sub) {
      center(label, x + w / 2, y + 4, FP, 1, ink);
      center(sub, x + w / 2, y + 15, FG, 1, subInk);
    } else {
      center(label, x + w / 2, y + (h - 2 - 8) / 2, FP, 1, ink);
    }
  }

  // Block bar: `segs` segments with 2px gaps, the first `filled` in `color`.
  void blocks(int x, int y, int w, int h, int segs, int filled, uint16_t color) {
    int segW = (w - 2 * (segs - 1)) / segs;
    int total = segW * segs + 2 * (segs - 1);
    x += (w - total) / 2;
    for (int i = 0; i < segs; i++) rect(x + i * (segW + 2), y, segW, h, i < filled ? color : C::panel2);
  }

  // Chip right-aligned at xr: small rounded state-colored rect, UPPERCASE label.
  int chip(int xr, int y, const char* label, uint16_t body, uint16_t ink = C::keyink) {
    int w = tw(label) + 10;
    rrect(xr - w, y, w, 14, body);
    text(label, xr - w + 5, y + 3, FP, 1, ink);
    return w;
  }

 private:
  Adafruit_ST7789& tft_;
  GFXcanvas16* canvas_ = nullptr;
  int oy_ = 0;
};
