#include "HUB73P481_ESP32.h"

#include <algorithm>
#include <new>
#include <string.h>
#include "panel_registers.h"

#if !defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32)
#error "HUB73P481_ESP32 requires Arduino-ESP32 on classic ESP32."
#endif

#include "panel_esp32.h"

namespace {
constexpr unsigned Scans = 7, Groups = 4, Chips = 8, Outputs = 16, Bits = 16;
constexpr unsigned LE_SAMPLE = 1u << 12;
constexpr unsigned PREAMBLE_A = 11, PREAMBLE_B = 14, VSYNC = 3;
constexpr unsigned LATCH_REGISTER = 5;
constexpr unsigned ScanCount = Scans * Outputs;
constexpr unsigned MaxGray = 0x0400;
// Confirmed Mario scan geometry, rows numbered 1 through 52.
constexpr uint8_t row_at_addr0[Groups][2] = {{52,46}, {39,33}, {26,20}, {13,7}};
HUB73P481_ESP32 *owner;
unsigned transaction;

static bool hardware_init() {
    const unsigned data = ScanCount * transaction;
    const unsigned cycle = esp_panel::ScanClocks;
    return esp_panel::start(((data + cycle - 1) / cycle) * cycle +
                            esp_panel::SyncScanClocks);
}

static void prepare_frame(unsigned words) {
    const unsigned cycle = esp_panel::ScanClocks;
    const unsigned boundary = ((words + cycle - 1) / cycle) * cycle;
    for (unsigned i = 0; i < boundary; ++i)
        esp_panel::frame[i] = esp_panel::scanWord(i % cycle, false);
    for (unsigned i = 0; i < esp_panel::SyncScanClocks; ++i)
        esp_panel::frame[boundary + i] = esp_panel::scanWord(i, true);
}

static uint32_t &sample_at(unsigned index) {
    return esp_panel::frame[index];
}

static bool present_frame(unsigned words) {
    const unsigned cycle = esp_panel::ScanClocks;
    return esp_panel::send(((words + cycle - 1) / cycle) * cycle +
                           esp_panel::SyncScanClocks, true);
}

static void append_idle(unsigned &index) {
    // Match the short clocked idle interval after each Pico transaction.
    for (unsigned i = 0; i < 20; ++i)
        sample_at(index++) = 0;
}

static void append_command(unsigned &index, unsigned clocks) {
    for (unsigned i = 0; i < clocks; ++i)
        sample_at(index++) = LE_SAMPLE;
    append_idle(index);
}

static void append_register(unsigned &index, uint16_t red, uint16_t green,
                            uint16_t blue) {
    for (unsigned i = 0; i < transaction; ++i) {
        const unsigned mask = 0x8000u >> (i % Bits);
        const unsigned rgb = ((red & mask) ? 1u : 0u) |
                             ((green & mask) ? 2u : 0u) |
                             ((blue & mask) ? 4u : 0u);
        sample_at(index++) = rgb * 0x249u |
            (i >= transaction - LATCH_REGISTER ? LE_SAMPLE : 0u);
    }
    append_idle(index);
}

static bool initialize_panel() {
    delayMicroseconds(1000);
    unsigned index = 0;
    append_command(index, VSYNC);
    if (!esp_panel::send(index, false))
        return false;
    for (const auto &rgb : panel_registers) {
        index = 0;
        append_command(index, PREAMBLE_A);
        append_command(index, PREAMBLE_B);
        append_register(index, 0x00aa, 0x00aa, 0x00aa);
        append_register(index, 0x01aa, 0x01aa, 0x01aa);
        append_register(index, rgb[0], rgb[1], rgb[2]);
        append_register(index, 0x0055, 0x0055, 0x0055);
        if (!esp_panel::send(index, false))
            return false;
    }
    return true;
}

uint16_t pwmWord(unsigned component, unsigned maxComponent, unsigned gray) {
    const unsigned steps = gray / 8;
    return ((component * steps + maxComponent / 2) / maxComponent) * 8;
}
} // namespace

HUB73P481_ESP32::HUB73P481_ESP32(uint8_t columns, uint8_t rows, ChainOrder order)
    : Adafruit_GFX(columns * TileSize, rows * TileSize),
      columns_(columns), rows_(rows), order_(order) {}

bool HUB73P481_ESP32::begin() {
    if (owner == this) {
        if (esp_panel::running)
            return true;
        delete[] pixels_;
        pixels_ = nullptr;
        owner = nullptr;
    }
    const unsigned count = unsigned(columns_) * rows_;
    if (owner || !count || count > MaxPanels)
        return false;

    transaction = count * Chips * Bits;
    pixels_ = new (std::nothrow) uint16_t[unsigned(WIDTH) * HEIGHT]();
    if (!pixels_ || !hardware_init() || !initialize_panel()) {
        esp_panel::stop();
        delete[] pixels_;
        pixels_ = nullptr;
        return false;
    }
    owner = this;
    if (show())
        return true;
    esp_panel::stop();
    delete[] pixels_;
    pixels_ = nullptr;
    owner = nullptr;
    return false;
}

HUB73P481_ESP32::~HUB73P481_ESP32() {
    if (owner != this)
        return;
    fillScreen(0);
    show();
    esp_panel::stop();
    delete[] pixels_;
    owner = nullptr;
}

void HUB73P481_ESP32::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (!pixels_ || x < 0 || y < 0 || x >= width() || y >= height())
        return;

    switch (getRotation()) {
    case 1:
        std::swap(x, y);
        x = WIDTH - 1 - x;
        break;
    case 2:
        x = WIDTH - 1 - x;
        y = HEIGHT - 1 - y;
        break;
    case 3:
        std::swap(x, y);
        y = HEIGHT - 1 - y;
        break;
    }
    pixels_[unsigned(y) * WIDTH + x] = color;
}

void HUB73P481_ESP32::fillScreen(uint16_t color) {
    if (pixels_)
        std::fill_n(pixels_, unsigned(WIDTH) * HEIGHT, color);
}

bool HUB73P481_ESP32::show() {
    if (owner != this)
        return false;
    if (!esp_panel::running)
        return false;

    const unsigned count = unsigned(columns_) * rows_;
    const unsigned gray = (unsigned(brightness_) * (MaxGray / 8) + 127) / 255 * 8;
    prepare_frame(ScanCount * transaction);

    for (unsigned addr = 0; addr < Scans; ++addr) {
        for (unsigned output = 0; output < Outputs; ++output) {
            const unsigned packet = (addr * Outputs + output) * transaction;
            sample_at(packet + transaction - 1) |= LE_SAMPLE;
            if (!gray || output < 3)
                continue;

            // Shift the most distant tile first, preserving chip order inside each tile.
            for (unsigned stream = 0; stream < count; ++stream) {
                const unsigned tile = count - 1 - stream;
                const unsigned tileY = tile / columns_;
                unsigned tileX = tile % columns_;
                if (order_ == ChainOrder::Serpentine && (tileY & 1))
                    tileX = columns_ - 1 - tileX;

                for (unsigned chip = 0; chip < Chips; ++chip) {
                    const unsigned bank = chip < 4 ? 1 : 0;
                    if (addr == 6 && bank == 0)
                        continue;
                    const unsigned x = tileX * TileSize + (TileSize - 1) -
                                       ((chip % 4) * 13 + output - 3);
                    const unsigned word = packet + (stream * Chips + chip) * Bits;
                    for (unsigned group = 0; group < Groups; ++group) {
                        const unsigned y = tileY * TileSize + row_at_addr0[group][bank] - 1 - addr;
                        const uint16_t color = pixels_[y * WIDTH + x];
                        if (!color)
                            continue;
                        const uint16_t red = pwmWord(color >> 11, 31, gray);
                        const uint16_t green = pwmWord((color >> 5) & 63, 63, gray);
                        const uint16_t blue = pwmWord(color & 31, 31, gray);
                        for (unsigned bit = 0; bit < 13; ++bit) {
                            const uint16_t mask = 0x8000u >> bit;
                            const unsigned rgb = ((red & mask) ? 1u : 0u) |
                                                 ((green & mask) ? 2u : 0u) |
                                                 ((blue & mask) ? 4u : 0u);
                            sample_at(word + bit) |= rgb << (group * 3);
                        }
                    }
                }
            }
        }
    }

    return present_frame(ScanCount * transaction);
}
