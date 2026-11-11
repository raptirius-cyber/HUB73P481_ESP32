#pragma once

#include <Adafruit_GFX.h>

class HUB73P481_ESP32 : public Adafruit_GFX {
public:
    enum class ChainOrder : uint8_t { RowMajor, Serpentine };
    static constexpr uint8_t TileSize = 52;
    static constexpr uint8_t MaxPanels = 4;

    explicit HUB73P481_ESP32(uint8_t columns = 1, uint8_t rows = 1,
                       ChainOrder order = ChainOrder::RowMajor);
    ~HUB73P481_ESP32();
    HUB73P481_ESP32(const HUB73P481_ESP32 &) = delete;
    HUB73P481_ESP32 &operator=(const HUB73P481_ESP32 &) = delete;

    bool begin();
    bool show();
    void setBrightness(uint8_t level) { brightness_ = level; }
    uint8_t getBrightness() const { return brightness_; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillScreen(uint16_t color) override;

    static constexpr uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
        return (uint16_t(r & 0xf8) << 8) | (uint16_t(g & 0xfc) << 3) | (b >> 3);
    }

private:
    const uint8_t columns_, rows_;
    const ChainOrder order_;
    uint8_t brightness_ = 128;
    uint16_t *pixels_ = nullptr;
};