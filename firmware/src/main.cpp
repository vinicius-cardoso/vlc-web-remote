// vlc-remote: retro (SNES dark) VLC knob. ESP32-C3 Super Mini + 2.4" ST7789
// + EC11, talking to the vlc-web-remote server over Wi-Fi.
#include <Arduino.h>
#include <SPI.h>

#if __has_include("config.h")
#include "config.h"
#else
#error "Missing src/config.h: cp src/config.example.h src/config.h and fill it in."
#endif

#include "board.h"
#include "bt.h"
#include "console.h"
#include "gfx.h"
#include "input.h"
#include "net.h"
#include "ui.h"

Adafruit_ST7789 tft(&SPI, PIN_CS, PIN_DC, PIN_RST);
Gfx gfx(tft);
Input input;
UI ui(gfx, input);
Console console(gfx, input, ui);

void setup() {
  Serial.setTxBufferSize(4096);  // room for screenshot bands
  Serial.begin(115200);

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, BL_ACTIVE_HIGH ? LOW : HIGH);  // dark until the first frame

  // Begin SPI on our pins first: the C3's default MISO is GPIO5, our DC line.
  SPI.begin(PIN_SCK, -1, PIN_MOSI, PIN_CS);
  tft.init(240, 320);
  tft.setSPISpeed(40000000);
  tft.invertDisplay(TFT_INVERT);
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(C::bg);

  if (!gfx.begin()) {
    Serial.println("[gfx] no memory for the frame band");
    for (;;) delay(1000);
  }
  input.begin();
  net::begin();
  ui.begin();
  Serial.printf("vlc-remote ready, server %s:%d\n", SERVER_HOST, SERVER_PORT);
}

void loop() {
  console.poll();
  ui.tick();
  bt::tick();
  delay(2);
}
