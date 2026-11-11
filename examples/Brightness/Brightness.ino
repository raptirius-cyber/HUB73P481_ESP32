#include <HUB73P481_ESP32.h>

HUB73P481_ESP32 matrix;

void setup() {
  Serial.begin(115200);
  if (!matrix.begin()) {
    Serial.println("HUB73 init failed");
    while (true) delay(1000);
  }
  for (int16_t x = 0; x < matrix.width(); ++x) {
    const uint8_t v = x * 255 / (matrix.width() - 1);
    matrix.drawFastVLine(x, 4, 10, matrix.color565(v, 0, 0));
    matrix.drawFastVLine(x, 16, 10, matrix.color565(0, v, 0));
    matrix.drawFastVLine(x, 28, 10, matrix.color565(0, 0, v));
    matrix.drawFastVLine(x, 40, 10, matrix.color565(v, v, v));
  }
}

void loop() {
  // Fade the same image without redrawing it.
  const uint16_t phase = (millis() / 8) % 512;
  matrix.setBrightness(phase < 256 ? phase : 511 - phase);
  matrix.show();
  delay(20);
}