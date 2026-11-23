// Network task (VLC mode). Keeps Wi-Fi up, polls /api/status, and sends what
// the UI asks for through /api/control. It runs as its own FreeRTOS task so a slow or dead
// server never freezes the screen or the knob; the UI reads a copy of the
// latest status under a mutex.
//
// Knob traffic is coalesced: the UI only sets the *latest* target volume and an
// accumulated seek delta, and this task sends them at a capped rate, so a fast
// spin becomes a handful of requests instead of one per detent.
#pragma once
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "config.h"
#include "text.h"

constexpr int MAX_TRACKS = 12;

struct Track {
  int16_t id = -1;
  bool selected = false;
  char title[40] = "";
};

// Stored in NVS. 1 was the removed PC mode; the UI reads it as VLC.
enum class Mode : uint8_t { Vlc = 0, Bt = 2 };

struct Status {
  uint8_t state = 0;  // 0 stopped, 1 playing, 2 paused
  int volume = 0;     // percent, 0-200
  int time = 0, length = 0;
  char title[96] = "";
  char artist[64] = "";
  Track audio[MAX_TRACKS];
  uint8_t nAudio = 0;
  Track subs[MAX_TRACKS];
  uint8_t nSubs = 0;
  int16_t activeAudio = -1, activeSub = -1;
};

enum class Link : uint8_t { Boot, NoWifi, NoServer, NoVlc, Ok };

namespace net {

constexpr uint32_t POLL_MS = 1000;          // status refresh while all is well
constexpr uint32_t POLL_OFFLINE_MS = 2500;  // ...and while the server is down
constexpr uint32_t VOLUME_EVERY_MS = 100;   // coalesced knob updates
constexpr uint32_t SEEK_EVERY_MS = 150;

static SemaphoreHandle_t mtx;
static TaskHandle_t taskHandle;

// shared state (guarded by mtx)
static Status status;
static Link link = Link::Boot;
static char detail[96] = "";  // SSID being tried, or the server's error
static uint32_t ver = 0;

struct Cmd {
  char action[16];
  int32_t value;
  bool hasValue;
};
static Cmd queue[8];
static uint8_t qHead = 0, qLen = 0;
static int pendingVol = -1;  // latest volume target, -1 = none
static int pendingSeek = 0;  // seconds still to seek

// Wi-Fi stays off until the UI picks VLC mode; BT mode never uses it.
static volatile Mode mode = Mode::Vlc;
static volatile bool started = false;
static volatile bool pollNow = false;

// Demo mode (serial console): the task goes quiet and the UI shows a fixed
// status, so screenshots never touch the real VLC or show real titles.
static volatile bool frozen = false;

// ---- UI side ----
static uint32_t version() { return ver; }

static void snapshot(Status& s, Link& l, char* d, size_t cap) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  s = status;
  l = link;
  strlcpy(d, detail, cap);
  xSemaphoreGive(mtx);
}

static bool isDemo() { return frozen; }

// Start (or switch to) a mode: drop what was queued for the old one and poll
// the new one right away.
static void setMode(Mode m) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  bool changed = mode != m || !started;
  mode = m;
  if (!frozen) started = true;  // demo screenshots never start the radio
  if (changed && !frozen) {
    qLen = 0;
    pendingVol = -1;
    pendingSeek = 0;
    status = Status();
    if (link == Link::NoVlc) {  // that error belonged to the old mode
      link = Link::Boot;
      detail[0] = 0;
    }
    pollNow = true;
  }
  ver++;
  xSemaphoreGive(mtx);
  xTaskNotifyGive(taskHandle);
}

static void send(const char* action, int32_t value = 0, bool hasValue = false) {
  if (frozen) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (qLen < 8) {
    Cmd& c = queue[(qHead + qLen++) % 8];
    strlcpy(c.action, action, sizeof c.action);
    c.value = value;
    c.hasValue = hasValue;
  }
  xSemaphoreGive(mtx);
  xTaskNotifyGive(taskHandle);
}

static void setVolume(int percent) {
  if (frozen) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  pendingVol = percent;
  xSemaphoreGive(mtx);
  xTaskNotifyGive(taskHandle);
}

static void seekBy(int seconds) {
  if (frozen) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  pendingSeek += seconds;
  xSemaphoreGive(mtx);
  xTaskNotifyGive(taskHandle);
}

// ---- task side ----
static void setLink(Link l, const char* d) {
  if (frozen) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  bool changed = link != l || strcmp(detail, d) != 0;
  link = l;
  fold(d, detail, sizeof detail, false);
  if (changed) ver++;
  xSemaphoreGive(mtx);
}

static const JsonDocument& filter() {
  static JsonDocument f;
  if (f.isNull()) {
    f["ok"] = true;
    f["error"] = true;
    JsonObject s = f["status"].to<JsonObject>();
    for (const char* k : {"state", "volumePercent", "time", "length", "title", "artist", "filename"})
      s[k] = true;
    JsonObject t = s["tracks"].to<JsonObject>();
    t["activeAudioId"] = true;
    t["activeSubtitleId"] = true;
    for (const char* list : {"audio", "subtitles"}) {
      JsonObject item = t[list].add<JsonObject>();
      item["id"] = true;
      item["title"] = true;
      item["selected"] = true;
    }
  }
  return f;
}

static uint8_t readTracks(JsonArrayConst arr, Track* out) {
  uint8_t n = 0;
  for (JsonObjectConst tr : arr) {
    if (n >= MAX_TRACKS) break;
    out[n].id = tr["id"] | -1;
    out[n].selected = tr["selected"] | false;
    fold(tr["title"] | "", out[n].title, sizeof out[n].title, true);
    n++;
  }
  return n;
}

static uint8_t playState(const char* state) {
  return !strcmp(state, "playing") ? 1 : !strcmp(state, "paused") ? 2 : 0;
}

static bool store(const Status& next, Mode m);

// One request to the server; on success the reply's status becomes the shared one.
static bool request(const char* path, const char* body) {
  const Mode m = mode;
  static WiFiClient client;
  static HTTPClient http;
  http.setReuse(true);  // the server keeps connections alive for 5 s
  http.setConnectTimeout(1500);
  http.setTimeout(2500);
  if (!http.begin(client, SERVER_HOST, SERVER_PORT, path)) {
    setLink(Link::NoServer, "BAD SERVER ADDRESS");
    return false;
  }
  int code;
  if (body) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST((uint8_t*)body, strlen(body));
  } else {
    code = http.GET();
  }
  if (code <= 0) {
    http.end();
    client.stop();
    setLink(Link::NoServer, HTTPClient::errorToString(code).c_str());
    return false;
  }
  String payload = http.getString();  // handles the chunked replies
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload, DeserializationOption::Filter(filter()))) {
    setLink(Link::NoServer, "BAD REPLY FROM SERVER");
    return false;
  }
  if (!(doc["ok"] | false)) {
    // 502: the server is up but VLC isn't answering. 400: a bad command.
    setLink(code == 502 ? Link::NoVlc : Link::Ok, doc["error"] | "SERVER ERROR");
    return false;
  }

  static Status next;  // static: keeps 1.2 KB off the task stack
  next = Status();
  JsonObjectConst s = doc["status"];
  next.volume = s["volumePercent"] | 0;
  next.state = playState(s["state"] | "stopped");
  next.time = s["time"] | 0;
  next.length = s["length"] | 0;
  const char* title = s["title"] | "";
  fold(*title ? title : (s["filename"] | ""), next.title, sizeof next.title, true);
  fold(s["artist"] | "", next.artist, sizeof next.artist, false);
  JsonObjectConst tracks = s["tracks"];
  next.nAudio = readTracks(tracks["audio"], next.audio);
  next.nSubs = readTracks(tracks["subtitles"], next.subs);
  next.activeAudio = tracks["activeAudioId"] | -1;
  next.activeSub = tracks["activeSubtitleId"] | -1;
  return store(next, m);
}

static bool store(const Status& next, Mode m) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (frozen || mode != m) {  // demo started, or the mode changed, mid-request
    xSemaphoreGive(mtx);
    return true;
  }
  status = next;
  link = Link::Ok;
  detail[0] = 0;
  ver++;
  xSemaphoreGive(mtx);
  return true;
}

static bool post(const char* action, int32_t value, bool hasValue) {
  char body[64];
  if (hasValue) snprintf(body, sizeof body, "{\"action\":\"%s\",\"value\":%ld}", action, (long)value);
  else snprintf(body, sizeof body, "{\"action\":\"%s\"}", action);
  Serial.printf("[net] %s\n", body);
  return request("/api/control", body);
}

static void connectWiFi() {
  for (int i = 0; i < WIFI_NET_COUNT && WiFi.status() != WL_CONNECTED; i++) {
    setLink(Link::NoWifi, WIFI_NETS[i].ssid);
    Serial.printf("[wifi] %s\n", WIFI_NETS[i].ssid);
    WiFi.begin(WIFI_NETS[i].ssid, WIFI_NETS[i].pass);
    // Many Super Mini boards have a weak antenna layout and fail to associate
    // at full power; 8.5 dBm is the usual fix.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    for (int t = 0; t < 40 && WiFi.status() != WL_CONNECTED; t++) vTaskDelay(pdMS_TO_TICKS(250));
    if (WiFi.status() != WL_CONNECTED) WiFi.disconnect();
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] OK %s\n", WiFi.localIP().toString().c_str());
    setLink(Link::NoServer, "CONNECTING");
  } else {
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

static void task(void*) {
  uint32_t lastPoll = 0, lastVol = 0, lastSeek = 0;
  bool radioOn = false;
  for (;;) {
    if (frozen || !started || mode == Mode::Bt) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (!radioOn) {
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);  // modem sleep adds 100+ ms to every knob request
      radioOn = true;
    }
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
      continue;
    }
    uint32_t now = millis();

    // 1) discrete commands, in order
    Cmd c;
    bool haveCmd = false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (qLen) {
      c = queue[qHead];
      qHead = (qHead + 1) % 8;
      qLen--;
      haveCmd = true;
    }
    xSemaphoreGive(mtx);
    if (haveCmd) {
      post(c.action, c.value, c.hasValue);
      lastPoll = millis();
      continue;
    }

    // 2) coalesced knob values
    int vol = -1, seek = 0;
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (pendingVol >= 0 && now - lastVol >= VOLUME_EVERY_MS) {
      vol = pendingVol;
      pendingVol = -1;
    } else if (pendingSeek != 0 && now - lastSeek >= SEEK_EVERY_MS) {
      seek = pendingSeek;
      pendingSeek = 0;
    }
    xSemaphoreGive(mtx);
    if (vol >= 0) {
      post("volumeSet", vol, true);
      lastVol = lastPoll = millis();
      continue;
    }
    if (seek != 0) {
      post("seekBy", seek, true);
      lastSeek = lastPoll = millis();
      continue;
    }

    // 3) status poll
    uint32_t every = link == Link::Ok ? POLL_MS : POLL_OFFLINE_MS;
    if (pollNow || now - lastPoll >= every) {
      pollNow = false;
      request("/api/status", nullptr);
      lastPoll = millis();
      continue;
    }

    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25));  // the UI wakes us on input
  }
}

// Show `s` with link state `l` until live() and stop all network traffic.
static void demo(const Status& s, Link l, const char* d) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  frozen = true;
  status = s;
  link = l;
  strlcpy(detail, d, sizeof detail);
  qLen = 0;
  pendingVol = -1;
  pendingSeek = 0;
  ver++;
  xSemaphoreGive(mtx);
}

static void live() {
  frozen = false;
  xTaskNotifyGive(taskHandle);
}

static void begin() {
  mtx = xSemaphoreCreateMutex();
  xTaskCreate(task, "net", 12288, nullptr, 1, &taskHandle);
}

}  // namespace net
