// BT mode: the knob pairs as a Bluetooth LE media remote (HID consumer
// control) and sends media keys. Works with any computer or phone, no server
// and no Wi-Fi. Keys only go one way, so there is no volume level or title to
// show: the OS decides how big each volume or brightness step is.
//
// Started once, when BT mode is picked; leaving BT mode restarts the board
// (see UI::start), so Bluetooth and Wi-Fi never run at the same time.
#pragma once
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

namespace bt {

constexpr const char* NAME = "VLC Knob";

// HID consumer page usages (USB HID Usage Tables, page 0x0C).
enum Usage : uint16_t {
  PLAY_PAUSE = 0xCD,
  NEXT = 0xB5,
  PREVIOUS = 0xB6,
  MUTE = 0xE2,
  VOL_UP = 0xE9,
  VOL_DOWN = 0xEA,
  BRIGHT_UP = 0x6F,
  BRIGHT_DOWN = 0x70,
};

// One report (ID 1): a single 16-bit consumer usage, 0 = released.
static const uint8_t REPORT_MAP[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (0x3FF)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (0x3FF)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xC0,              // End Collection
};

constexpr uint32_t GAP_MS = 12;  // between press and release notifications
constexpr uint8_t QUEUE = 32;

static NimBLEServer* server = nullptr;
static NimBLECharacteristic* input = nullptr;
static bool demoMode = false, demoLinked = false;

static uint16_t queue[QUEUE];
static uint8_t qHead = 0, qLen = 0;
static bool held = false;  // the queue head is pressed, its release is next
static uint32_t lastSend = 0;

static void begin() {
  if (server) return;
  NimBLEDevice::init(NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  // "Just Works" pairing with bonding: no PIN, the host remembers the knob.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  server = NimBLEDevice::createServer();
  static NimBLEHIDDevice hid(server);
  input = hid.inputReport(1);
  hid.manufacturer()->setValue("vlc-web-remote");
  hid.pnp(0x02, 0xE502, 0xA111, 0x0210);
  hid.hidInfo(0x00, 0x01);
  hid.reportMap((uint8_t*)REPORT_MAP, sizeof REPORT_MAP);
  hid.startServices();
  hid.setBatteryLevel(100);

  NimBLEAdvertising* adv = server->getAdvertising();
  adv->setAppearance(0x03C1);  // HID keyboard
  adv->addServiceUUID(hid.hidService()->getUUID());
  adv->setScanResponse(true);
  adv->start();  // NimBLE advertises again by itself after a disconnect
  Serial.printf("[bt] advertising as \"%s\"\n", NAME);
}

static bool connected() {
  if (demoMode) return demoLinked;
  return server && server->getConnectedCount() > 0;
}

// Queue one key tap (press + release).
static void tap(uint16_t usage) {
  if (demoMode || !connected() || qLen >= QUEUE) return;
  queue[(qHead + qLen++) % QUEUE] = usage;
}

// Sends the queue, one notification every GAP_MS. Call from loop().
static void tick() {
  if (!input || !qLen || millis() - lastSend < GAP_MS) return;
  if (!connected()) {
    qLen = 0;
    held = false;
    return;
  }
  uint16_t usage = held ? 0 : queue[qHead];
  uint8_t report[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  input->setValue(report, sizeof report);
  input->notify();
  lastSend = millis();
  if (held) {
    qHead = (qHead + 1) % QUEUE;
    qLen--;
  }
  held = !held;
}

// Serial console screenshots: pretend to be (dis)connected, send nothing.
static void demo(bool linked) {
  demoMode = true;
  demoLinked = linked;
}
static void live() { demoMode = false; }

}  // namespace bt
