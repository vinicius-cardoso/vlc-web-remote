// Retro look: SNES dark (the house choice from the retro-style skill), same as
// claude-usage-display and nfc-tag-manager. 16 roles, used by name, never raw hex.
#pragma once
#include <stdint.h>

static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// snes dark, from `palette.py snes dark`. Role meanings in palette.md.
namespace C {
constexpr uint16_t bg      = rgb565(0x1A, 0x1A, 0x21);  // app background
constexpr uint16_t panel   = rgb565(0x27, 0x27, 0x30);  // cards, rows
constexpr uint16_t panel2  = rgb565(0x36, 0x36, 0x41);  // inset, separators, empty bar
constexpr uint16_t ink     = rgb565(0xEC, 0xEC, 0xF2);  // main text
constexpr uint16_t dim     = rgb565(0x8A, 0x89, 0x96);  // secondary text
constexpr uint16_t key     = rgb565(0x43, 0x42, 0x4F);  // unselected keys/tabs
constexpr uint16_t key2    = rgb565(0x11, 0x11, 0x17);  // header strip, borders, 2px shadow
constexpr uint16_t keyink  = rgb565(0xEC, 0xEC, 0xF2);  // text on key/accent/color fills
constexpr uint16_t red     = rgb565(0xE0, 0x4A, 0x55);  // error  (A)
constexpr uint16_t yellow  = rgb565(0xEE, 0xC2, 0x44);  // warning(B) — text on it uses key2
constexpr uint16_t green   = rgb565(0x45, 0xB0, 0x7C);  // success(X)
constexpr uint16_t blue    = rgb565(0x62, 0x74, 0xD0);  // info   (Y)
constexpr uint16_t purple  = rgb565(0x6B, 0x58, 0xC2);  // secondary accent
constexpr uint16_t lav     = rgb565(0x4A, 0x44, 0x86);  // selection fill, badges (fill only)
constexpr uint16_t white   = rgb565(0x34, 0x34, 0x3E);  // light key face (dark theme: a grey)
constexpr uint16_t accent  = rgb565(0x6B, 0x58, 0xC2);  // focus, selected, primary, links
}  // namespace C
