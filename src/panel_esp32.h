#pragma once

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_rom_gpio.h>
#include <esp_private/periph_ctrl.h>
#include <soc/gpio_sig_map.h>
#include <soc/i2s_struct.h>
#include <soc/lldesc.h>

namespace esp_panel {

// Four RGB triplets, then LE, A/B/C and ROW. WS supplies DCLK.
constexpr gpio_num_t Pins[] = {
    GPIO_NUM_4, GPIO_NUM_13, GPIO_NUM_14,
    GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18,
    GPIO_NUM_19, GPIO_NUM_21, GPIO_NUM_22,
    GPIO_NUM_23, GPIO_NUM_25, GPIO_NUM_26,
    GPIO_NUM_27, GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_2, GPIO_NUM_15
};
constexpr gpio_num_t ClockPin = GPIO_NUM_5;
constexpr unsigned RowClocks = 312;
constexpr unsigned ScanClocks = 7 * RowClocks + 4;
constexpr unsigned VsyncClocks = 3;
constexpr unsigned FirstRowExtraClocks = 9;
constexpr unsigned SyncScanClocks = ScanClocks + VsyncClocks + FirstRowExtraClocks;
constexpr unsigned RowBit = 1u << 16;
constexpr unsigned BlockWords = 1020; // DMA descriptors carry at most 4092 bytes.
constexpr unsigned MaxWords = 4 * 7 * 16 * 8 * 16 + ScanClocks + SyncScanClocks;
constexpr unsigned MaxBlocks = (MaxWords + BlockWords - 1) / BlockWords;

struct Wave {
    uint32_t *blocks[MaxBlocks]{};
    lldesc_t *descriptors = nullptr;
    unsigned count = 0;

    uint32_t &operator[](unsigned index) {
        return blocks[index / BlockWords][index % BlockWords];
    }

    void release() {
        for (unsigned i = 0; i < count; ++i) {
            heap_caps_free(blocks[i]);
            blocks[i] = nullptr;
        }
        heap_caps_free(descriptors);
        descriptors = nullptr;
        count = 0;
    }

    bool allocate(unsigned words) {
        count = (words + BlockWords - 1) / BlockWords;
        descriptors = static_cast<lldesc_t *>(
            heap_caps_calloc(count, sizeof(lldesc_t), MALLOC_CAP_DMA));
        if (!descriptors)
            return false;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned capacity = words < BlockWords ? words : BlockWords;
            blocks[i] = static_cast<uint32_t *>(
                heap_caps_calloc(capacity, sizeof(uint32_t), MALLOC_CAP_DMA));
            if (!blocks[i])
                return false;
            words -= capacity;
        }
        return true;
    }

    lldesc_t *link(unsigned words, lldesc_t *next, bool eof) {
        unsigned i = 0;
        while (words) {
            const unsigned length = words < BlockWords ? words : BlockWords;
            words -= length;
            auto &d = descriptors[i];
            d.size = d.length = length * sizeof(uint32_t);
            d.buf = reinterpret_cast<uint8_t *>(blocks[i]);
            d.owner = 1;
            d.eof = !words && eof;
            d.qe.stqe_next = words ? &descriptors[i + 1] : next;
            ++i;
        }
        return &descriptors[i - 1];
    }
};

static Wave frame, idle[2];
static lldesc_t *idleTail;
static unsigned currentIdle;
static bool running;

static uint32_t scanWord(unsigned phase, bool sync) {
    if (sync && phase < VsyncClocks)
        return (6u << 13) | (1u << 12); // Keep the last address while LE is high.
    const unsigned elapsed = sync ? phase - VsyncClocks : phase;
    if (sync && elapsed < RowClocks + FirstRowExtraClocks)
        return elapsed >= 2 && elapsed < 14 ? RowBit : 0;
    const unsigned scanPhase = sync ? elapsed - FirstRowExtraClocks : elapsed;
    const unsigned address = scanPhase / RowClocks;
    if (address >= 7)
        return 6u << 13;
    const unsigned slot = scanPhase % RowClocks;
    return (address << 13) | (slot >= 2 && slot < 6 ? RowBit : 0);
}

static void route(gpio_num_t pin, int signal, bool invert = false) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(pin, signal, invert, false);
}

static void stop() {
    if (running) {
        I2S0.conf.tx_start = 0;
        I2S0.out_link.stop = 1;
        periph_module_reset(PERIPH_I2S0_MODULE);
        periph_module_disable(PERIPH_I2S0_MODULE);
        for (const auto pin : Pins)
            gpio_reset_pin(pin);
        gpio_reset_pin(ClockPin);
        running = false;
    }
    frame.release();
    idle[0].release();
    idle[1].release();
}

static bool start(unsigned words) {
    if (!frame.allocate(words) || !idle[0].allocate(ScanClocks) ||
        !idle[1].allocate(ScanClocks)) {
        stop();
        return false;
    }
    currentIdle = 0;
    idleTail = idle[0].link(ScanClocks, idle[0].descriptors, true);

    periph_module_enable(PERIPH_I2S0_MODULE);
    periph_module_reset(PERIPH_I2S0_MODULE);
    I2S0.conf.val = 0;
    I2S0.conf1.val = 0;
    I2S0.conf2.val = 0;
    I2S0.conf2.lcd_en = 1;
    I2S0.conf2.lcd_tx_wrx2_en = 1; // LCD form 1, one sample per WS period.
    I2S0.timing.val = 0;
    I2S0.clkm_conf.val = 0; // 160 MHz PLL / 5 / 2 for BCK, then / 2 for WS.
    I2S0.clkm_conf.clkm_div_a = 1;
    I2S0.clkm_conf.clkm_div_num = 5;
    I2S0.sample_rate_conf.val = 0;
    I2S0.sample_rate_conf.tx_bits_mod = 24;
    I2S0.sample_rate_conf.tx_bck_div_num = 2;
    I2S0.fifo_conf.val = 0;
    I2S0.fifo_conf.dscr_en = 1;
    I2S0.fifo_conf.tx_data_num = 32;
    I2S0.fifo_conf.tx_fifo_mod = 3; // One 32-bit DMA word per parallel sample.
    I2S0.fifo_conf.tx_fifo_mod_force_en = 1;
    I2S0.conf_chan.val = 0;
    I2S0.conf_chan.tx_chan_mod = 1;
    I2S0.lc_conf.val = 0;
    I2S0.lc_conf.out_eof_mode = 1;
    I2S0.lc_conf.outdscr_burst_en = 1;
    I2S0.lc_conf.out_data_burst_en = 1;
    I2S0.int_ena.val = 0;
    I2S0.int_clr.val = 0xffffffffu;

    for (unsigned bit = 0; bit < sizeof(Pins) / sizeof(Pins[0]); ++bit)
        route(Pins[bit], I2S0O_DATA_OUT0_IDX + bit);
    route(ClockPin, I2S0O_WS_OUT_IDX, true);
    __sync_synchronize();
    I2S0.out_link.addr = reinterpret_cast<uintptr_t>(idle[0].descriptors) & 0xfffffu;
    I2S0.out_link.start = 1;
    I2S0.conf.tx_start = 1;
    running = true;
    return true;
}

static bool send(unsigned words, bool scan) {
    if (!running)
        return false;
    // Leave through the current ring's tail. The destination ring never
    // points back to the one-shot waveform, even if the task is preempted.
    auto &next = idle[1 - currentIdle];
    for (unsigned i = 0; i < ScanClocks; ++i)
        next[i] = scan ? scanWord(i, false) : 0;
    lldesc_t *nextTail = next.link(ScanClocks, next.descriptors, true);
    frame.link(words, next.descriptors, false);
    __sync_synchronize();
    idleTail->qe.stqe_next = frame.descriptors;
    __sync_synchronize();

    const uint32_t startTime = micros();
    while (I2S0.out_eof_des_addr != reinterpret_cast<uintptr_t>(nextTail)) {
        if (I2S0.int_raw.out_dscr_err || uint32_t(micros() - startTime) >= 50000) {
            stop();
            return false;
        }
    }
    currentIdle = 1 - currentIdle;
    idleTail = nextTail;
    return true;
}

} // namespace esp_panel
