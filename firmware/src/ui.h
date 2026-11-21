// Screens and controls.
//
//   HOME  knob turn = volume          K0 click = play/pause   K0 hold = mute
//         knob click = SEEK mode      knob hold = MENU
//   SEEK  knob turn = jump 10/30/60 s (faster spin, bigger jump)
//         knob click or 4 s idle = back to HOME
//   MENU  AUDIO / SUBTITLES / CLOSE; knob turns and clicks, K0 = back,
//         knob hold or 8 s idle = close
//
// The knob always falls back to volume, so it never seeks by surprise.
// Values the user just set (volume, play state, seek target) are shown
// optimistically until the server's status catches up.
#pragma once
#include "gfx.h"
#include "input.h"
#include "net.h"
#include "text.h"

class UI {
 public:
  UI(Gfx& g, Input& in) : g_(g), in_(in) {}

  void begin() {
    lastInput_ = millis();
    backlight(255);
  }

  // Back to the home screen with no optimistic values (serial console).
  void reset() {
    scr_ = Screen::Home;
    volHold_ = playHold_ = 0;
    playOverride_ = -1;
    dirty_ = true;
  }

  // Draw right now, regardless of the dirty flag (serial screenshots).
  void render() {
    dirty_ = false;
    lastFrame_ = millis();
    g_.frame([this] { draw(); });
  }

  void tick() {
    uint32_t now = millis();

    if (net::version() != seenVer_) {
      seenVer_ = net::version();
      net::snapshot(st_, link_, detail_, sizeof detail_);
      if (link_ != Link::Ok && scr_ != Screen::Home) scr_ = Screen::Home;
      dirty_ = true;
    }

    int det = in_.takeDetents() * ENC_DIR;
    Input::Btn b = in_.pollButtons();
    if (det || b != Input::Btn::None) {
      lastInput_ = now;
      if (dimmed_) { dimmed_ = false; backlight(255); }
    }
    if (det) turn(det, now);
    if (b != Input::Btn::None) press(b, now);

    // timeouts
    if (scr_ == Screen::Seek && now > seekUntil_) go(Screen::Home);
    if (inMenu() && now > menuUntil_) go(Screen::Home);
    if (volHold_ && now > volHold_) { volHold_ = 0; dirty_ = true; }
    if (playHold_ && now > playHold_) { playHold_ = 0; playOverride_ = -1; dirty_ = true; }
    if (link_ != Link::Ok && now - lastBlink_ >= 400) {
      lastBlink_ = now;
      blink_ = !blink_;
      dirty_ = true;
    }
    if (!dimmed_ && now - lastInput_ > DIM_AFTER_MS) {
      dimmed_ = true;
      backlight(DIM_LEVEL);
    }

    if (dirty_ && now - lastFrame_ >= 30) {
      dirty_ = false;
      lastFrame_ = now;
      g_.frame([this] { draw(); });
    }
  }

 private:
  enum class Screen : uint8_t { Home, Seek, Menu, Audio, Subs };
  static constexpr uint32_t SEEK_TIMEOUT = 4000, MENU_TIMEOUT = 8000, HOLD_MS = 1500;
  static constexpr int ROWS = 5;  // menu rows that fit

  Gfx& g_;
  Input& in_;
  Screen scr_ = Screen::Home;
  Status st_;
  Link link_ = Link::Boot;
  char detail_[96] = "";
  uint32_t seenVer_ = 0;

  int uiVol_ = 0, muteRestore_ = 100;
  uint32_t volHold_ = 0, lastVolTurn_ = 0;
  int8_t playOverride_ = -1;  // 1 playing, 0 paused, -1 = trust the server
  uint32_t playHold_ = 0;
  int seekBase_ = 0, seekDelta_ = 0, seekStep_ = 10;
  uint32_t seekUntil_ = 0, lastSeekTurn_ = 0;

  Track list_[MAX_TRACKS + 1];
  int nList_ = 0, sel_ = 0, top_ = 0;
  uint32_t menuUntil_ = 0;

  bool dirty_ = true, dimmed_ = false, blink_ = false;
  uint32_t lastFrame_ = 0, lastInput_ = 0, lastBlink_ = 0;

  // ---------------------------------------------------------------- state ---
  bool online() const { return link_ == Link::Ok; }
  bool inMenu() const { return scr_ == Screen::Menu || scr_ == Screen::Audio || scr_ == Screen::Subs; }
  bool playing() const { return playOverride_ >= 0 ? playOverride_ == 1 : st_.state == 1; }
  int volume() const { return volHold_ ? uiVol_ : st_.volume; }
  int shownTime() const { return scr_ == Screen::Seek ? seekBase_ + seekDelta_ : st_.time; }

  void go(Screen s) {
    scr_ = s;
    dirty_ = true;
  }

  static void backlight(int level) {
    analogWrite(PIN_BL, BL_ACTIVE_HIGH ? level : 255 - level);
  }

  // Faster spin -> bigger steps. dt = ms since the previous detent.
  static int speed(uint32_t dt) { return dt < 40 ? 3 : dt < 90 ? 2 : 1; }

  void setVolume(int v, uint32_t now) {
    uiVol_ = constrain(v, 0, VOL_MAX);
    volHold_ = now + HOLD_MS;
    net::setVolume(uiVol_);
  }

  // ---------------------------------------------------------------- input ---
  void turn(int det, uint32_t now) {
    dirty_ = true;
    if (!online()) return;
    switch (scr_) {
      case Screen::Home: {
        static const int MUL[4] = {0, 1, 2, 4};
        int mul = MUL[speed(now - lastVolTurn_)];
        lastVolTurn_ = now;
        setVolume(volume() + det * VOL_STEP * mul, now);
        break;
      }
      case Screen::Seek: {
        static const int STEP[4] = {0, 10, 30, 60};
        seekStep_ = STEP[speed(now - lastSeekTurn_)];
        lastSeekTurn_ = now;
        int target = seekBase_ + seekDelta_ + det * seekStep_;
        target = constrain(target, 0, st_.length > 1 ? st_.length - 1 : 0);
        int change = target - (seekBase_ + seekDelta_);
        if (change) {
          seekDelta_ += change;
          net::seekBy(change);
        }
        seekUntil_ = now + SEEK_TIMEOUT;
        break;
      }
      default: {
        int n = scr_ == Screen::Menu ? 3 : nList_;
        sel_ = constrain(sel_ + det, 0, n > 0 ? n - 1 : 0);
        if (sel_ < top_) top_ = sel_;
        if (sel_ >= top_ + ROWS) top_ = sel_ - ROWS + 1;
        menuUntil_ = now + MENU_TIMEOUT;
        break;
      }
    }
  }

  void press(Input::Btn b, uint32_t now) {
    using B = Input::Btn;
    dirty_ = true;
    if (!online()) return;

    if (inMenu()) {
      menuUntil_ = now + MENU_TIMEOUT;
      if (b == B::KnobLong || b == B::KeyLong) return go(Screen::Home);
      if (b == B::KeyClick) {  // back
        if (scr_ == Screen::Menu) return go(Screen::Home);
        sel_ = scr_ == Screen::Audio ? 0 : 1;
        top_ = 0;
        return go(Screen::Menu);
      }
      return choose(now);  // knob click
    }

    switch (b) {
      case B::KeyClick:  // play / pause
        net::send("toggle");
        playOverride_ = playing() ? 0 : 1;
        playHold_ = now + HOLD_MS;
        break;
      case B::KeyLong: {  // mute / unmute
        int v = volume();
        if (v > 0) {
          muteRestore_ = v;
          setVolume(0, now);
        } else {
          setVolume(muteRestore_ > 0 ? muteRestore_ : 100, now);
        }
        break;
      }
      case B::KnobClick:
        if (scr_ == Screen::Seek) {
          go(Screen::Home);
        } else if (st_.length > 0) {
          seekBase_ = st_.time;
          seekDelta_ = 0;
          seekStep_ = 10;
          seekUntil_ = now + SEEK_TIMEOUT;
          go(Screen::Seek);
        }
        break;
      case B::KnobLong:
        sel_ = top_ = 0;
        menuUntil_ = now + MENU_TIMEOUT;
        go(Screen::Menu);
        break;
      default:
        break;
    }
  }

  void choose(uint32_t now) {
    if (scr_ == Screen::Menu) {
      if (sel_ == 2) return go(Screen::Home);
      bool audio = sel_ == 0;
      nList_ = 0;
      if (!audio) {  // subtitles start with an OFF row
        list_[0] = Track();
        list_[0].id = -1;
        strlcpy(list_[0].title, "OFF", sizeof list_[0].title);
        nList_ = 1;
      }
      const Track* src = audio ? st_.audio : st_.subs;
      int n = audio ? st_.nAudio : st_.nSubs;
      for (int i = 0; i < n; i++) list_[nList_++] = src[i];
      int active = audio ? st_.activeAudio : st_.activeSub;
      sel_ = 0;
      for (int i = 0; i < nList_; i++)
        if (list_[i].id == active) sel_ = i;
      top_ = sel_ >= ROWS ? sel_ - ROWS + 1 : 0;
      return go(audio ? Screen::Audio : Screen::Subs);
    }
    if (nList_ == 0) return;
    const Track& t = list_[sel_];
    if (scr_ == Screen::Audio) {
      net::send("audioTrack", t.id, true);
      st_.activeAudio = t.id;
    } else if (t.id < 0) {
      net::send("subtitleOff");
      st_.activeSub = -1;
    } else {
      net::send("subtitleTrack", t.id, true);
      st_.activeSub = t.id;
    }
    go(Screen::Home);
  }

  // ----------------------------------------------------------------- draw ---
  void draw() {
    header();
    if (!online()) {
      offline();
      keys("PLAY", "SEEK", "MENU", false);
      return;
    }
    switch (scr_) {
      case Screen::Home:
        nowPlaying();
        volumeCard();
        keys(playing() ? "PAUSE" : "PLAY", "SEEK", "MENU", true);
        break;
      case Screen::Seek:
        nowPlaying();
        seekCard();
        keys(playing() ? "PAUSE" : "PLAY", "DONE", "MENU", true);
        break;
      default:
        menu();
        keys("BACK", "SELECT", "CLOSE", true, false);
        break;
    }
  }

  // Header strip: state on the left, A B X Y dots, mode chip on the right.
  void header() {
    g_.rect(0, 0, SCR_W, 22, C::key2);
    g_.rect(0, 22, SCR_W, 2, C::accent);

    const char* label;
    if (!online()) {
      label = "VLC REMOTE";
    } else if (inMenu()) {
      label = scr_ == Screen::Audio ? "AUDIO" : scr_ == Screen::Subs ? "SUBTITLES" : "MENU";
    } else {
      label = playing() ? "PLAYING" : st_.state == 2 || playOverride_ == 0 ? "PAUSED" : "STOPPED";
      if (playing()) g_.playIcon(8, 8, C::green);
      else if (st_.state == 2 || playOverride_ == 0) g_.pauseIcon(8, 8, C::yellow);
      else g_.stopIcon(8, 8, C::dim);
    }
    g_.text(label, inMenu() || !online() ? 8 : 22, 8, FP, 1, C::ink);

    const uint16_t dots[4] = {C::red, C::yellow, C::green, C::blue};
    for (int i = 0; i < 4; i++) g_.disc(188 + i * 12, 11, 4, dots[i]);

    if (!online()) g_.chip(314, 4, "OFF", C::red);
    else if (scr_ == Screen::Seek) g_.chip(314, 4, "SEEK", C::yellow, C::key2);
    else if (inMenu()) g_.chip(314, 4, "MENU", C::accent);
    else g_.chip(314, 4, "VOL", C::blue);
  }

  // Now-playing card: title (2 lines), artist, block progress bar, times.
  void nowPlaying() {
    const int x = 6, y = 30, w = 308, h = 94;
    g_.card(x, y, w, h);
    if (scr_ == Screen::Seek) g_.rect(x + 1, y + 1, 4, h - 2, C::yellow);

    if (st_.title[0]) {
      char lines[2][64];
      int n = wrap(st_.title, (w - 20) / 8, 2, lines);
      for (int i = 0; i < n; i++) g_.text(lines[i], x + 10, y + 10 + i * 12, FP, 1, C::ink);
    } else {
      g_.text("NOTHING PLAYING", x + 10, y + 10, FP, 1, C::dim);
    }
    if (st_.artist[0]) g_.text(Gfx::fit(st_.artist, w - 20, FG), x + 10, y + 36, FG, 1, C::dim);

    int t = shownTime(), len = st_.length;
    int filled = len > 0 ? (int)((long)t * 20 / len) : 0;
    g_.blocks(x + 10, y + 52, w - 20, 12, 20, filled, scr_ == Screen::Seek ? C::yellow : C::accent);

    char cur[12], total[12], left[14];
    fmtTime(t, cur, sizeof cur);
    if (len > 0) {
      fmtTime(len, total, sizeof total);
      left[0] = '-';
      fmtTime(len - t, left + 1, sizeof left - 1);
      g_.text(cur, x + 10, y + 74, FP, 1, C::ink);
      int cx = x + 10 + tw(cur) + 8;
      g_.text("/", cx, y + 74, FP, 1, C::dim);
      g_.text(total, cx + 16, y + 74, FP, 1, C::dim);
      g_.rtext(left, x + w - 10, y + 74, FP, 1, C::dim);
    } else if (st_.state != 0) {
      g_.text(cur, x + 10, y + 74, FP, 1, C::ink);
      g_.rtext("LIVE", x + w - 10, y + 74, FP, 1, C::dim);
    }
  }

  // Volume: label, big percent in the level color, 20-block bar over 0-200%.
  void volumeCard() {
    const int x = 6, y = 130, w = 308, h = 70;
    int v = volume();
    uint16_t col = v == 0 ? C::dim : v > 150 ? C::red : v > 100 ? C::yellow : C::green;
    g_.card(x, y, w, h);
    g_.text("VOLUME", x + 10, y + 12, FP, 1, C::ink);
    if (v == 0) g_.text("MUTED", x + 66, y + 12, FP, 1, C::red);
    else if (v > 100) g_.text("BOOST", x + 66, y + 12, FP, 1, C::yellow);

    char buf[8];
    snprintf(buf, sizeof buf, "%d%%", v);
    g_.rtext(buf, x + w - 10, y + 8, FP, 3, col);
    g_.blocks(x + 10, y + 44, w - 20, 14, 20, (v + 5) / 10, col);
  }

  // Seek: big signed jump in yellow, the target time, the current step.
  void seekCard() {
    const int x = 6, y = 130, w = 308, h = 70;
    g_.card(x, y, w, h);
    g_.rect(x + 1, y + 1, 4, h - 2, C::yellow);
    g_.text("SEEK", x + 12, y + 12, FP, 1, C::ink);

    char d[14], to[16], step[10];
    d[0] = seekDelta_ < 0 ? '-' : '+';
    fmtTime(abs(seekDelta_), d + 1, sizeof d - 1);
    g_.rtext(d, x + w - 10, y + 8, FP, 3, C::yellow);

    strcpy(to, "TO ");
    fmtTime(seekBase_ + seekDelta_, to + 3, sizeof to - 3);
    g_.text(to, x + 12, y + 46, FP, 1, C::ink);
    snprintf(step, sizeof step, "%dS STEP", seekStep_);
    g_.rtext(step, x + w - 10, y + 46, FP, 1, C::dim);
  }

  // Menu and track lists: keycap-style rows, accent on the selected one.
  void menu() {
    const int x = 6, w = 308, rh = 30, gap = 4, y0 = 30;
    if (scr_ == Screen::Menu) {
      const char* labels[3] = {"AUDIO", "SUBTITLES", "CLOSE"};
      char audio[40] = "-", subs[40] = "OFF";
      for (int i = 0; i < st_.nAudio; i++)
        if (st_.audio[i].id == st_.activeAudio) strlcpy(audio, st_.audio[i].title, sizeof audio);
      for (int i = 0; i < st_.nSubs; i++)
        if (st_.subs[i].id == st_.activeSub) strlcpy(subs, st_.subs[i].title, sizeof subs);
      const char* values[3] = {audio, subs, ""};
      for (int i = 0; i < 3; i++) row(x, y0 + i * (rh + gap), w, rh, labels[i], values[i], i == sel_, false);
      return;
    }
    if (nList_ == 0) {
      g_.card(x, y0, w, rh);
      g_.text("NO TRACKS", x + 12, y0 + 11, FP, 1, C::dim);
      return;
    }
    int active = scr_ == Screen::Audio ? st_.activeAudio : st_.activeSub;
    for (int r = 0; r < ROWS && top_ + r < nList_; r++) {
      const Track& t = list_[top_ + r];
      row(x, y0 + r * (rh + gap), w, rh, t.title, "", top_ + r == sel_, t.id == active);
    }
    if (top_ > 0) g_.triangle(SCR_W - 4, y0 + 2, true, C::dim);
    if (top_ + ROWS < nList_) g_.triangle(SCR_W - 4, y0 + ROWS * (rh + gap) - gap - 7, false, C::dim);
  }

  void row(int x, int y, int w, int h, const char* label, const char* value, bool selected, bool on) {
    g_.card(x, y, w, h, selected ? C::accent : C::panel);
    if (selected) g_.rect(x + 1, y + 1, 4, h - 2, C::lav);
    uint16_t ink = selected ? C::keyink : C::ink;
    int right = x + w - 10;
    if (on) right -= g_.chip(right, y + 8, "ON", C::green) + 8;
    int lw = tw(label) + 12;
    if (value && *value) {
      g_.text(label, x + 12, y + 11, FP, 1, ink);
      g_.rtext(Gfx::fit(value, right - (x + lw + 8), FG), right, y + 11, FG, 1, selected ? C::keyink : C::dim);
    } else {
      g_.text(Gfx::fit(label, right - (x + 12) - 4), x + 12, y + 11, FP, 1, ink);
    }
  }

  // Bottom keycaps: what K0, a knob click and a knob hold do right now.
  void keys(const char* k0, const char* click, const char* hold, bool enabled, bool primary = true) {
    const char* labels[3] = {k0, click, hold};
    static const char* SUB[3] = {"BUTTON", "KNOB CLICK", "KNOB HOLD"};
    for (int i = 0; i < 3; i++) {
      uint16_t body = !enabled ? C::panel2 : (i == 0 && primary) ? C::accent : C::key;
      g_.key(6 + i * 105, 206, 98, 30, body, labels[i], SUB[i], enabled ? C::keyink : C::dim,
             (i == 0 && primary && enabled) ? C::keyink : C::dim);
    }
  }

  // Offline: what's wrong, a blinking lamp, and what to do about it.
  void offline() {
    const int x = 6, y = 30, w = 308, h = 170;
    g_.card(x, y, w, h);
    g_.rect(x + 1, y + 1, 4, h - 2, C::yellow);

    const char* big;
    uint16_t col;
    const char* hint1 = "";
    const char* hint2 = "";
    // Demo screenshots go into a public README: placeholder addresses only.
    bool demo = net::isDemo();
    char where[48];
    snprintf(where, sizeof where, "%s:%d", demo ? "192.168.0.10" : SERVER_HOST, demo ? 3000 : SERVER_PORT);
    switch (link_) {
      case Link::NoWifi:
        big = "NO WIFI";
        col = C::red;
        hint1 = "TRYING NETWORK";
        hint2 = detail_;
        break;
      case Link::NoVlc:
        big = "VLC CLOSED";
        col = C::yellow;
        hint1 = "OPEN VLC WITH ITS HTTP INTERFACE:";
        hint2 = "./scripts/start-vlc.sh";
        break;
      case Link::NoServer:
        big = "NO SERVER";
        col = C::red;
        hint1 = "START IT WITH npm start ON";
        hint2 = where;
        break;
      default:
        big = "STARTING";
        col = C::yellow;
        break;
    }
    g_.disc(x + 26, y + 34, 7, blink_ ? C::yellow : C::red);
    g_.text("!", x + 23, y + 31, FP, 1, C::key2);
    g_.text(big, x + 46, y + 26, FP, 2, col);

    if (*hint1) g_.text(Gfx::fit(hint1, w - 24, FG), x + 14, y + 62, FG, 1, C::ink);
    if (*hint2) g_.text(Gfx::fit(hint2, w - 24, FG), x + 14, y + 76, FG, 1, C::dim);
    if (link_ == Link::NoServer && *detail_) {
      g_.text(Gfx::fit(detail_, w - 24, FG), x + 14, y + 98, FG, 1, C::red);
    }
    if (link_ == Link::NoVlc && *detail_) {
      char lines[2][64];
      int n = wrap(detail_, (w - 24) / 6, 2, lines);
      for (int i = 0; i < n; i++) g_.text(lines[i], x + 14, y + 98 + i * 12, FG, 1, C::dim);
    }
    if (link_ != Link::NoWifi) {
      char ip[32];
      snprintf(ip, sizeof ip, "ME %s", demo ? "192.168.0.42" : WiFi.localIP().toString().c_str());
      g_.text(ip, x + 14, y + h - 18, FG, 1, C::dim);
    }
  }
};
