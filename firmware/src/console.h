// Serial console for screenshots and bench testing. One command per line:
//
//   demo play|novlc|noserver|nowifi   show a fixed demo status, stop network
//   live                              back to the real server
//   home                              reset the UI to the home screen
//   turn N                            turn the knob N detents (negative = back)
//   click | hold                      knob click / knob hold
//   key | keyhold                     K0 click / K0 hold
//   shot                              dump the screen (see tools/screenshot.py)
//
// `shot` prints "FRAME 320 240", then 320*240 RGB565 little-endian pixels,
// then "END". Demo data is made up (Big Buck Bunny, Blender Foundation, CC BY).
#pragma once
#include "gfx.h"
#include "input.h"
#include "net.h"
#include "ui.h"

class Console {
 public:
  Console(Gfx& g, Input& in, UI& ui) : g_(g), in_(in), ui_(ui) {}

  void poll() {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r') continue;
      if (c != '\n') {
        if (len_ < sizeof line_ - 1) line_[len_++] = c;
        continue;
      }
      line_[len_] = 0;
      len_ = 0;
      run(line_);
    }
  }

 private:
  Gfx& g_;
  Input& in_;
  UI& ui_;
  char line_[48];
  size_t len_ = 0;

  void run(const char* cmd) {
    using B = Input::Btn;
    if (!strncmp(cmd, "demo ", 5)) demo(cmd + 5);
    else if (!strcmp(cmd, "live")) net::live();
    else if (!strcmp(cmd, "home")) ui_.reset();
    else if (!strncmp(cmd, "turn ", 5)) in_.inject(atoi(cmd + 5));
    else if (!strcmp(cmd, "click")) in_.inject(B::KnobClick);
    else if (!strcmp(cmd, "hold")) in_.inject(B::KnobLong);
    else if (!strcmp(cmd, "key")) in_.inject(B::KeyClick);
    else if (!strcmp(cmd, "keyhold")) in_.inject(B::KeyLong);
    else if (!strcmp(cmd, "shot")) shot();
    else if (*cmd) { Serial.printf("? %s\n", cmd); return; }
    Serial.println("OK");
  }

  void shot() {
    Serial.printf("FRAME %d %d\n", SCR_W, SCR_H);
    g_.capture = true;
    ui_.render();
    g_.capture = false;
    Serial.println();
    Serial.println("END");
  }

  static void track(Track& t, int id, const char* title, bool selected = false) {
    t.id = id;
    t.selected = selected;
    strlcpy(t.title, title, sizeof t.title);
  }

  void demo(const char* which) {
    Status s;
    s.state = 1;
    s.volume = 85;
    s.time = 214;
    s.length = 596;
    strlcpy(s.title, "BIG BUCK BUNNY", sizeof s.title);
    strlcpy(s.artist, "Blender Foundation - 2008", sizeof s.artist);
    track(s.audio[0], 1, "STEREO - ENGLISH", true);
    track(s.audio[1], 2, "5.1 SURROUND - ENGLISH");
    track(s.audio[2], 3, "DIRECTOR COMMENTARY - ENGLISH");
    s.nAudio = 3;
    track(s.subs[0], 4, "ENGLISH");
    track(s.subs[1], 5, "PORTUGUES (BRASIL)", true);
    track(s.subs[2], 6, "ESPANOL");
    track(s.subs[3], 7, "FRANCAIS");
    s.nSubs = 4;
    s.activeAudio = 1;
    s.activeSub = 5;

    if (!strcmp(which, "novlc"))
      net::demo(s, Link::NoVlc, "Nao foi possivel conectar ao VLC em http://127.0.0.1:8080.");
    else if (!strcmp(which, "noserver"))
      net::demo(s, Link::NoServer, "connection refused");
    else if (!strcmp(which, "nowifi"))
      net::demo(s, Link::NoWifi, "HOME-WIFI");
    else
      net::demo(s, Link::Ok, "");
  }
};
