#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#ifndef PIN_RGB_LED
#define PIN_RGB_LED 48
#endif

static Adafruit_NeoPixel rgb(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

static void setColor(uint8_t r, uint8_t g, uint8_t b) {
  rgb.setPixelColor(0, rgb.Color(r, g, b));
  rgb.show();
}

void setup() {
  rgb.begin();
  rgb.setBrightness(80);
  setColor(0, 0, 0);
}

void loop() {
  setColor(255, 0, 0);
  delay(120);
  setColor(0, 0, 0);
  delay(40);

  setColor(0, 0, 255);
  delay(120);
  setColor(0, 0, 0);
  delay(40);
}
