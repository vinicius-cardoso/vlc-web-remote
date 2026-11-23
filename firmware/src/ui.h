// Screens and controls.
//
//   PICK  at boot: VLC or BT. Knob turn chooses, knob click or K0 starts it.
//         The last choice is remembered and preselected.
//
// VLC mode (the VLC player, through /api/control):
//   HOME  knob turn = volume          K0 click = play/pause   K0 hold = mute
//         knob click = SEEK mode      knob hold = MENU
//   SEEK  knob turn = jump 10/30/60 s (faster spin, bigger jump)
//         knob click or 4 s idle = back to HOME
//   MENU  AUDIO / SUBTITLES / MODE / CLOSE; knob turns and clicks, K0 = back,
//         knob hold or 8 s idle = close
//
// BT mode (Bluetooth media keys, any computer or phone, no server):
//   HOME  knob turn = volume, brightness or track (prev/next), by target
//         knob click = next target (VOL > LIGHT > TRACK), 5 s idle = VOL
//         K0 click = play/pause       K0 hold = mute          knob hold = PICK
//   Keys only go one way, so there are no levels or titles to show.
//
// Offline, a knob hold still opens PICK, so a closed VLC never traps you.
// Moving between Wi-Fi (VLC) and Bluetooth (BT) restarts the board straight
// into the new mode, so the two radios never run together.
//
// The knob always falls back to volume, so it never seeks by surprise.
// Values the user just set (volume, play state, seek target) are shown
// optimistically until the server's status catches up.
#pragma once
#include <Preferences.h>
#include "bt.h"
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
    prefs_.begin("knob", false);
    pick_ = index(stored("mode"));
    scr_ = Screen::Pick;
    if (prefs_.isKey("auto")) {  // restarted on purpose to change radios
      Mode m = stored("auto");
      prefs_.remove("auto");
      start(m, false);
    }
  }

  // Run mode `m` from its home screen; `remember` makes it the next default.
  void start(Mode m, bool remember) {
    bool demo = net::isDemo();
    if (remember) prefs_.putUChar("mode", (uint8_t)m);
    Radio want = m == Mode::Bt ? Radio::Ble : Radio::Wifi;
    if (!demo && radio_ != Radio::None && radio_ != want) {
      prefs_.putUChar("auto", (uint8_t)m);
      // No Serial.flush() here: with USB plugged in but no terminal reading,
      // it waits forever and the knob looks frozen.
      Serial.println("[ui] switching radios, restarting");
      delay(20);
      ESP.restart();
    }
    mode_ = m;
    if (m == Mode::Bt) {
      if (!demo) bt::begin();
    } else {
      net::setMode(m);
    }
    if (!demo) radio_ = want;
    reset();
  }

  // Show the mode picker, with the current mode preselected.
  void pick() {
    pick_ = index(mode_);
    go(Screen::Pick);
  }

  // Back to the home screen with no optimistic values (serial console).
  void reset() {
    scr_ = Screen::Home;
    tgt_ = Target::Vol;
    volHold_ = playHold_ = skipShow_ = 0;
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
      if (link_ != Link::Ok && scr_ != Screen::Home && scr_ != Screen::Pick) scr_ = Screen::Home;
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
    if (bt() && bt::connected() != btLinked_) {
      btLinked_ = !btLinked_;
      dirty_ = true;
    }

    // timeouts
    if (scr_ == Screen::Seek && now > seekUntil_) go(Screen::Home);
    if (inMenu() && now > menuUntil_) go(Screen::Home);
    if (volHold_ && now > volHold_) { volHold_ = 0; dirty_ = true; }
    if (playHold_ && now > playHold_) { playHold_ = 0; playOverride_ = -1; dirty_ = true; }
    if (skipShow_ && now > skipShow_) { skipShow_ = 0; dirty_ = true; }
    if (tgt_ != Target::Vol && now > tgtUntil_) { tgt_ = Target::Vol; dirty_ = true; }
    bool alert = scr_ != Screen::Pick && (bt() ? !btLinked_ : link_ != Link::Ok);
    if (alert && now - lastBlink_ >= 400) {
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
  enum class Screen : uint8_t { Pick, Home, Seek, Menu, Audio, Subs };
  enum class Target : uint8_t { Vol, Light, Track };  // BT knob target
  static constexpr uint32_t SEEK_TIMEOUT = 4000, MENU_TIMEOUT = 8000, HOLD_MS = 1500;
  static constexpr uint32_t TARGET_TIMEOUT = 5000, SKIP_GAP_MS = 350;
  static constexpr int ROWS = 5;  // menu rows that fit

  Gfx& g_;
  Input& in_;
  Preferences prefs_;
  Screen scr_ = Screen::Pick;
  enum class Radio : uint8_t { None, Wifi, Ble };
  Mode mode_ = Mode::Vlc;
  Radio radio_ = Radio::None;  // the radio in use since boot
  int pick_ = 0;  // picker card: 0 VLC, 1 BT
  bool btLinked_ = false;
  char lastKey_[12] = "";  // BT: last key sent, and how many times in a row
  int keyCount_ = 0;
  uint32_t lastKeyAt_ = 0;
  Target tgt_ = Target::Vol;
  uint32_t tgtUntil_ = 0;
  int8_t skipDir_ = 0;  // BT: last left (-1) / right (1) turn, shown until skipShow_
  uint32_t skipShow_ = 0, lastSkip_ = 0;
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
  bool bt() const { return mode_ == Mode::Bt; }

  // Picker card <-> mode. NVS keeps the Mode value (VLC 0, BT 2); anything
  // else, like a stored 1 from the removed PC mode, means VLC.
  static int index(Mode m) { return m == Mode::Bt ? 1 : 0; }
  static Mode modeAt(int i) { return i ? Mode::Bt : Mode::Vlc; }
  Mode stored(const char* key) { return prefs_.getUChar(key, 0) == (uint8_t)Mode::Bt ? Mode::Bt : Mode::Vlc; }

  Target nextTarget() const {
    if (tgt_ == Target::Vol) return Target::Light;
    return tgt_ == Target::Light ? Target::Track : Target::Vol;
  }
  static const char* targetName(Target t) {
    return t == Target::Vol ? "VOL" : t == Target::Light ? "LIGHT" : "TRACK";
  }

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
    if (scr_ == Screen::Pick) {
      pick_ = constrain(pick_ + det, 0, 1);
      return;
    }
    if (bt()) return turnBt(det, now);
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
        int n = scr_ == Screen::Menu ? 4 : nList_;
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
    if (scr_ == Screen::Pick) {
      if (b == B::KnobClick || b == B::KeyClick) start(modeAt(pick_), true);
      return;
    }
    if (bt()) return pressBt(b, now);
    if (!online()) {
      if (b == B::KnobLong) pick();
      return;
    }

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

  // ------------------------------------------------------------------ bt ---
  void sendKey(uint16_t usage, const char* name, uint32_t now) {
    bt::tap(usage);
    if (!strcmp(name, lastKey_) && now - lastKeyAt_ < 2000) keyCount_++;
    else {
      strlcpy(lastKey_, name, sizeof lastKey_);
      keyCount_ = 1;
    }
    lastKeyAt_ = now;
  }

  void turnBt(int det, uint32_t now) {
    if (!bt::connected()) return;
    if (tgt_ != Target::Vol) tgtUntil_ = now + TARGET_TIMEOUT;
    bool up = det > 0;
    if (tgt_ == Target::Track) {
      if (now - lastSkip_ < SKIP_GAP_MS) return;  // one track per gesture
      lastSkip_ = now;
      sendKey(up ? bt::NEXT : bt::PREVIOUS, up ? "NEXT" : "PREV", now);
    } else {
      bool vol = tgt_ == Target::Vol;  // one key per detent: the OS sets the step
      for (int i = 0; i < abs(det); i++) {
        if (vol) sendKey(up ? bt::VOL_UP : bt::VOL_DOWN, up ? "VOL+" : "VOL-", now);
        else sendKey(up ? bt::BRIGHT_UP : bt::BRIGHT_DOWN, up ? "BRIGHT+" : "BRIGHT-", now);
      }
    }
    skipDir_ = up ? 1 : -1;
    skipShow_ = now + (tgt_ == Target::Track ? HOLD_MS : 400);
  }

  void pressBt(Input::Btn b, uint32_t now) {
    using B = Input::Btn;
    switch (b) {
      case B::KeyClick:
        if (bt::connected()) sendKey(bt::PLAY_PAUSE, "PLAY/PAUSE", now);
        break;
      case B::KeyLong:
        if (bt::connected()) sendKey(bt::MUTE, "MUTE", now);
        break;
      case B::KnobClick:
        tgt_ = nextTarget();
        tgtUntil_ = now + TARGET_TIMEOUT;
        break;
      case B::KnobLong:
        pick();
        break;
      default:
        break;
    }
  }

  void choose(uint32_t now) {
    if (scr_ == Screen::Menu) {
      if (sel_ == 3) return go(Screen::Home);
      if (sel_ == 2) return pick();
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
    if (scr_ == Screen::Pick) {
      pickScreen();
      keys("START", "START", "-", 0b011);
      return;
    }
    if (bt()) {
      btStatus();
      btCard();
      keys("PLAY", targetName(nextTarget()), "MODE", btLinked_ ? 0b111 : 0b110);
      return;
    }
    if (!online()) {
      offline();
      keys("PLAY", "SEEK", "MODE", 0b100, false);
      return;
    }
    switch (scr_) {
      case Screen::Home:
        nowPlaying();
        volumeCard();
        keys(playing() ? "PAUSE" : "PLAY", "SEEK", "MENU", 0b111);
        break;
      case Screen::Seek:
        nowPlaying();
        seekCard();
        keys(playing() ? "PAUSE" : "PLAY", "DONE", "MENU", 0b111);
        break;
      default:
        menu();
        keys("BACK", "SELECT", "CLOSE", 0b111, false);
        break;
    }
  }

  // Mode picker: two big cards, the chosen one in accent.
  void pickScreen() {
    static const char* NAME[2] = {"VLC", "BT"};
    static const char* WHAT[2] = {"VLC VIA THE SERVER", "ANY COMPUTER OR PHONE"};
    static const char* DOES[2][3] = {{"VOLUME, SEEK", "AUDIO TRACKS", "SUBTITLES"},
                                     {"MEDIA KEYS", "VOLUME, BRIGHTNESS", "NO SERVER, NO WIFI"}};
    const int y = 30, w = 151, h = 142;
    for (int i = 0; i < 2; i++) {
      int x = 6 + i * (w + 6), cx = x + w / 2;
      bool on = pick_ == i;
      uint16_t ink = on ? C::keyink : C::ink, sub = on ? C::keyink : C::dim;
      g_.card(x, y, w, h, on ? C::accent : C::panel);
      if (on) g_.rect(x + 1, y + 1, 4, h - 2, C::lav);
      if (i == 0) bigPlay(cx - 8, y + 16, on ? C::keyink : C::yellow);
      else rune(cx - 9, y + 16, on ? C::keyink : C::blue);
      g_.center(NAME[i], cx, y + 50, FP, 3, ink);
      g_.center(WHAT[i], cx, y + 82, FG, 1, sub);
      for (int k = 0; k < 3; k++) g_.center(DOES[i][k], cx, y + 102 + k * 12, FG, 1, sub);
    }
    g_.center("TURN TO CHOOSE, CLICK TO START", SCR_W / 2, 182, FG, 1, C::dim);
  }

  // 18x24 stepped play triangle.
  void bigPlay(int x, int y, uint16_t c) {
    for (int r = 0; r < 24; r++) {
      int k = r < 12 ? r : 23 - r;
      g_.rect(x, y + r, 2 + (k / 2) * 3, 1, c);
    }
  }

  // 18x24 Bluetooth rune (9x12 pixels at 2x).
  void rune(int x, int y, uint16_t c) {
    static const char* ART[12] = {"....#....", "....##...", "....#.#..", ".#..#..#.",
                                  "..#.#.#..", "...###...", "...###...", "..#.#.#..",
                                  ".#..#..#.", "....#.#..", "....##...", "....#...."};
    for (int r = 0; r < 12; r++)
      for (int k = 0; k < 9; k++)
        if (ART[r][k] == '#') g_.rect(x + k * 2, y + r * 2, 2, 2, c);
  }

  // BT status card: pairing instructions, or connected and the last key sent.
  void btStatus() {
    const int x = 6, y = 30, w = 308, h = 94;
    g_.card(x, y, w, h);
    if (!btLinked_) {
      g_.rect(x + 1, y + 1, 4, h - 2, C::yellow);
      g_.disc(x + 26, y + 24, 7, blink_ ? C::yellow : C::blue);
      g_.text("PAIRING", x + 46, y + 16, FP, 2, C::yellow);
      g_.text("ON THE COMPUTER OR PHONE, OPEN", x + 14, y + 46, FG, 1, C::ink);
      g_.text("BLUETOOTH > ADD DEVICE AND PICK", x + 14, y + 60, FG, 1, C::ink);
      g_.text(bt::NAME, x + 14, y + 76, FP, 1, C::blue);
      return;
    }
    g_.disc(x + 26, y + 24, 7, C::green);
    g_.text("CONNECTED", x + 46, y + 16, FP, 2, C::green);
    g_.text("SENDING MEDIA KEYS. THE COMPUTER", x + 14, y + 46, FG, 1, C::dim);
    g_.text("SETS THE SIZE OF EACH STEP.", x + 14, y + 58, FG, 1, C::dim);
    if (lastKey_[0]) {
      char buf[20];
      if (keyCount_ > 1) snprintf(buf, sizeof buf, "%s X%d", lastKey_, keyCount_);
      else strlcpy(buf, lastKey_, sizeof buf);
      g_.text("LAST KEY", x + 14, y + 76, FG, 1, C::dim);
      g_.rtext(buf, x + w - 10, y + 74, FP, 1, C::ink);
    }
  }

  // BT target card: what turning the knob sends; the side just sent flashes.
  void btCard() {
    const int x = 6, y = 130, w = 308, h = 70;
    bool vol = tgt_ == Target::Vol, light = tgt_ == Target::Light;
    g_.card(x, y, w, h);
    g_.rect(x + 1, y + 1, 4, h - 2, vol ? C::blue : light ? C::yellow : C::accent);
    g_.text(vol ? "VOLUME" : light ? "BRIGHTNESS" : "TRACK", x + 12, y + 12, FP, 1, C::ink);
    g_.rtext("TURN THE KNOB", x + w - 10, y + 12, FG, 1, C::dim);
    uint16_t idle = btLinked_ ? C::ink : C::dim;
    bool left = skipShow_ && skipDir_ < 0, right = skipShow_ && skipDir_ > 0;
    g_.text(vol ? "<VOL-" : light ? "<DIM" : "<PREV", x + 12, y + 38, FP, 2, left ? C::yellow : idle);
    g_.rtext(vol ? "VOL+>" : light ? "BRIGHT>" : "NEXT>", x + w - 10, y + 38, FP, 2, right ? C::yellow : idle);
  }

  // Header strip: state on the left, A B X Y dots, mode chip on the right.
  void header() {
    g_.rect(0, 0, SCR_W, 22, C::key2);
    g_.rect(0, 22, SCR_W, 2, C::accent);

    const char* label;
    bool icon = false;
    if (scr_ == Screen::Pick) {
      label = "SELECT MODE";
    } else if (bt()) {
      icon = true;
      label = btLinked_ ? "BT CONNECTED" : "BT PAIRING";
      g_.disc(11, 11, 4, btLinked_ ? C::green : blink_ ? C::yellow : C::blue);
    } else if (!online()) {
      label = "VLC REMOTE";
    } else if (inMenu()) {
      label = scr_ == Screen::Audio ? "AUDIO" : scr_ == Screen::Subs ? "SUBTITLES" : "MENU";
    } else {
      icon = true;
      label = playing() ? "PLAYING" : st_.state == 2 || playOverride_ == 0 ? "PAUSED" : "STOPPED";
      if (playing()) g_.playIcon(8, 8, C::green);
      else if (st_.state == 2 || playOverride_ == 0) g_.pauseIcon(8, 8, C::yellow);
      else g_.stopIcon(8, 8, C::dim);
    }
    g_.text(label, icon ? 22 : 8, 8, FP, 1, C::ink);

    const uint16_t dots[4] = {C::red, C::yellow, C::green, C::blue};
    for (int i = 0; i < 4; i++) g_.disc(188 + i * 12, 11, 4, dots[i]);

    if (scr_ == Screen::Pick) g_.chip(314, 4, "MODE", C::accent);
    else if (bt() && !btLinked_) g_.chip(314, 4, "PAIR", C::yellow, C::key2);
    else if (!bt() && !online()) g_.chip(314, 4, "OFF", C::red);
    else if (bt() && tgt_ == Target::Light) g_.chip(314, 4, "LIGHT", C::yellow, C::key2);
    else if (bt() && tgt_ == Target::Track) g_.chip(314, 4, "TRACK", C::accent);
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
    bool mute = v == 0;
    uint16_t col = mute ? C::dim : v > 150 ? C::red : v > 100 ? C::yellow : C::green;
    g_.card(x, y, w, h);
    g_.text("VOLUME", x + 10, y + 12, FP, 1, C::ink);
    if (mute) g_.text("MUTED", x + 66, y + 12, FP, 1, C::red);
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
      const char* labels[4] = {"AUDIO", "SUBTITLES", "MODE", "CLOSE"};
      char audio[40] = "-", subs[40] = "OFF";
      for (int i = 0; i < st_.nAudio; i++)
        if (st_.audio[i].id == st_.activeAudio) strlcpy(audio, st_.audio[i].title, sizeof audio);
      for (int i = 0; i < st_.nSubs; i++)
        if (st_.subs[i].id == st_.activeSub) strlcpy(subs, st_.subs[i].title, sizeof subs);
      const char* values[4] = {audio, subs, "VLC / BT", ""};
      for (int i = 0; i < 4; i++) row(x, y0 + i * (rh + gap), w, rh, labels[i], values[i], i == sel_, false);
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
  // `enabled` is a bit mask: bit 0 = K0, bit 1 = knob click, bit 2 = knob hold.
  void keys(const char* k0, const char* click, const char* hold, uint8_t enabledMask, bool primary = true) {
    const char* labels[3] = {k0, click, hold};
    static const char* SUB[3] = {"BUTTON", "KNOB CLICK", "KNOB HOLD"};
    for (int i = 0; i < 3; i++) {
      bool enabled = enabledMask & (1 << i);
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
