// Text helpers for the ASCII-only bitmap fonts: UTF-8 folding, time format,
// word wrap. Text that doesn't fit is cut with a trailing '~' (house rule).
#pragma once
#include <Arduino.h>

// UTF-8 -> printable ASCII. Latin-1 letters lose their accents (Português ->
// Portugues), typographic quotes and dashes become ASCII, anything else '?'.
// upper=true also uppercases, for labels.
static void fold(const char* in, char* out, size_t cap, bool upper) {
  static const char LAT[] =
      "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYPs" "aaaaaaaceeeeiiii" "dnooooo/ouuuuypy";
  size_t n = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && n + 1 < cap) {
    char c;
    if (*p < 0x80) {
      c = (char)*p++;
      if (c < 0x20) c = ' ';
    } else if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
      c = LAT[p[1] - 0x80];
      p += 2;
    } else if (*p == 0xC2 && p[1] == 0xA0) {
      c = ' ';
      p += 2;
    } else if (*p == 0xE2 && p[1] == 0x80 && p[2]) {
      uint8_t k = p[2];
      c = (k == 0x98 || k == 0x99) ? '\''
        : (k == 0x9C || k == 0x9D) ? '"'
        : (k == 0x93 || k == 0x94) ? '-'
        : (k == 0xA6) ? '.' : '?';
      p += 3;
    } else {
      c = '?';
      p++;
      while ((*p & 0xC0) == 0x80) p++;  // skip continuation bytes
    }
    if (upper && c >= 'a' && c <= 'z') c -= 32;
    out[n++] = c;
  }
  out[n] = 0;
}

// Seconds -> "M:SS" or "H:MM:SS".
static void fmtTime(int s, char* out, size_t cap) {
  if (s < 0) s = 0;
  int h = s / 3600, m = (s / 60) % 60, sec = s % 60;
  if (h) snprintf(out, cap, "%d:%02d:%02d", h, m, sec);
  else snprintf(out, cap, "%d:%02d", m, sec);
}

// Word-wraps str into up to maxLines lines of maxChars (< 64). A '~' marks
// text that didn't fit. Returns the number of lines. (From claude-usage-display.)
static int wrap(const char* str, int maxChars, int maxLines, char out[][64]) {
  int n = 0, len = 0;
  char cur[64];
  const char* p = str;
  bool more = false;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* w = p;
    while (*p && *p != ' ') p++;
    int wl = p - w;
    while (wl > 0) {
      int room = len ? maxChars - len - 1 : maxChars;
      if (wl <= room) {
        if (len) cur[len++] = ' ';
        memcpy(cur + len, w, wl);
        len += wl;
        wl = 0;
      } else if (n == maxLines - 1 || (!len && n < maxLines)) {
        int take = room > 0 ? room : 0;
        if (len && room > 0) cur[len++] = ' ', take--;
        if (take > 0) { memcpy(cur + len, w, take); len += take; w += take; wl -= take; }
        cur[len] = 0;
        memcpy(out[n++], cur, len + 1);
        len = 0;
        if (n >= maxLines) { more = wl > 0 || *p; break; }
      } else {
        cur[len] = 0;
        memcpy(out[n++], cur, len + 1);
        len = 0;
      }
    }
    if (n >= maxLines) { more = more || *p; break; }
  }
  if (len && n < maxLines) { cur[len] = 0; memcpy(out[n++], cur, len + 1); }
  if (more && n) {
    int l = strlen(out[n - 1]);
    if (l >= maxChars) l = maxChars - 1;
    out[n - 1][l] = '~';
    out[n - 1][l + 1] = 0;
  }
  return n;
}
