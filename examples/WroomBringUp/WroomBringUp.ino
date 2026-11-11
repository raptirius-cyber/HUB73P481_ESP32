#include <HUB73P481_ESP32.h>

HUB73P481_ESP32 matrix;

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("HUB73P481_ESP32: starting one 52x52 panel");
  if (!matrix.begin()) {
    Serial.println("HUB73P481_ESP32: begin failed");
    return;
  }

  matrix.setBrightness(96);
  matrix.fillScreen(0);
  matrix.drawRect(0, 0, 52, 52, 0xffff);
  matrix.fillRect(5, 5, 12, 12, 0xf800);
  matrix.fillRect(20, 5, 12, 12, 0x07e0);
  matrix.fillRect(35, 5, 12, 12, 0x001f);
  matrix.drawLine(4, 46, 47, 22, 0xffe0);
  matrix.setCursor(6, 25);
  matrix.setTextColor(0xffff);
  matrix.print("HUB73");

  const uint32_t started = micros();
  if (matrix.show())
    Serial.printf("HUB73P481_ESP32: show OK, %lu us\n", (unsigned long)(micros() - started));
  else
    Serial.println("HUB73P481_ESP32: show failed");
}

void loop() {
  delay(1000);
}
