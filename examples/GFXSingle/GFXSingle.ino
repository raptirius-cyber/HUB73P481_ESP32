#include <HUB73P481_ESP32.h>

HUB73P481_ESP32 matrix;

void setup() {
  Serial.begin(115200);
  if (!matrix.begin()) {
    Serial.println("HUB73 init failed");
    while (true) delay(1000);
  }
  matrix.fillScreen(0);
  matrix.drawRect(0, 0, 52, 52, 0xffff);
  matrix.setTextColor(0x07ff);
  matrix.setCursor(11, 10);
  matrix.print("HUB73");
  matrix.fillCircle(12, 34, 5, 0xf800);
  matrix.fillCircle(26, 34, 5, 0x07e0);
  matrix.fillCircle(40, 34, 5, 0x001f);
  matrix.show();
}

void loop() {}