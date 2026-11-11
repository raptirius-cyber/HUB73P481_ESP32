#include <HUB73P481_ESP32.h>

// Choose 2,1 for 104x52, 1,2 for 52x104, or 2,2 for 104x104.
constexpr uint8_t columns = 2;
constexpr uint8_t rows = 1;
HUB73P481_ESP32 matrix(columns, rows, HUB73P481_ESP32::ChainOrder::RowMajor);

void setup() {
  Serial.begin(115200);
  if (!matrix.begin()) {
    Serial.println("HUB73 init failed");
    while (true) delay(1000);
  }
  matrix.setTextSize(2);
}

void loop() {
  static int16_t step = 0;
  matrix.fillScreen(0);
  const uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffe0};
  for (uint8_t y = 0; y < rows; ++y) {
    for (uint8_t x = 0; x < columns; ++x) {
      const uint8_t index = y * columns + x;
      const int16_t left = x * HUB73P481_ESP32::TileSize;
      const int16_t top = y * HUB73P481_ESP32::TileSize;
      matrix.drawRect(left, top, 52, 52, colors[index]);
      matrix.setTextColor(colors[index]);
      matrix.setCursor(left + 4, top + 5);
      matrix.print(index + 1);
    }
  }
  matrix.drawLine(0, 0, matrix.width() - 1, matrix.height() - 1, 0x07ff);
  const int16_t span = max(matrix.width(), matrix.height());
  matrix.fillCircle(step * (matrix.width() - 1) / (span - 1),
                    step * (matrix.height() - 1) / (span - 1), 3, 0xffff);
  matrix.show();
  step = (step + 1) % span;
  delay(25);
}