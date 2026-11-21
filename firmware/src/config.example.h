// config.example.h: template. Copy to config.h (gitignored) and fill in.
//   cp src/config.example.h src/config.h
#pragma once

// ---------- WI-FI ----------
// Tried in order until one connects.
struct WifiNet { const char* ssid; const char* pass; };
const WifiNet WIFI_NETS[] = {
  { "HOME_SSID", "HOME_PASSWORD" },
};
const int WIFI_NET_COUNT = sizeof(WIFI_NETS) / sizeof(WIFI_NETS[0]);

// ---------- SERVER ----------
// The vlc-web-remote server (npm start), not VLC itself. PORT comes from .env.
// Use the computer's LAN IP; a DHCP reservation keeps it from changing.
#define SERVER_HOST "192.168.0.10"
#define SERVER_PORT 3000

// ---------- SCREEN ----------
// Landscape: 3 with the knob on the right (this module), 1 if upside down.
#define SCREEN_ROTATION 3
// Backlight (BLK pin): 1 if HIGH turns it on.
#define BL_ACTIVE_HIGH 1
// Dim the backlight after this long without touching the knob or button.
#define DIM_AFTER_MS 120000
#define DIM_LEVEL 24  // 0-255

// ---------- KNOB ----------
// 1 or -1: flip it if turning clockwise lowers the volume.
#define ENC_DIR 1
// Volume per detent (percent) at slow speed; spinning fast multiplies it.
#define VOL_STEP 2
// Knob ceiling in percent. VLC goes up to 200; above 100 is software boost.
#define VOL_MAX 200
