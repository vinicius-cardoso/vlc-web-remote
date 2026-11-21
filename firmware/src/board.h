// ESP32-C3 Super Mini + 2.4" ST7789 (320x240 landscape) + EC11 module.
// Pins from docs/wiring/esp32c3-tft-ec11.json. GPIO 2, 8 and 9 are boot
// strapping pins and stay unused.
#pragma once
#include <stdint.h>

// display (SPI). The module's SCL/SDA are SPI clock/data, not I2C.
constexpr int PIN_SCK = 4;    // U2.3 SCL
constexpr int PIN_MOSI = 6;   // U2.4 SDA
constexpr int PIN_CS = 7;     // U2.7 CS
constexpr int PIN_DC = 5;     // U2.6 DC
constexpr int PIN_RST = 20;   // U2.5 RES
constexpr int PIN_BL = 21;    // U2.8 BLK (PWM)

// EC11 knob and the module's K0 button. The module has its own pull-ups;
// the internal ones are enabled as a backup.
constexpr int ENC_A = 0;      // U2.9 A
constexpr int ENC_B = 1;      // U2.10 B
constexpr int ENC_SW = 3;     // U2.11 PUSH (knob press)
constexpr int KEY0 = 10;      // U2.12 KO (square button)

constexpr int SCR_W = 320;
constexpr int SCR_H = 240;
// This panel renders with inversion OFF (same module as nfc-tag-manager).
constexpr bool TFT_INVERT = false;
