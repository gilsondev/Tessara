// Guition JC3248W535 (Fikra ESP35): ESP32-S3 N16R8, AXS15231B 320x480 QSPI panel with I2C touch, I2S amp, BOOT key.
#include "hal/board.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

#include <cstring>

namespace {
constexpr int kWidth = 320;
constexpr int kHeight = 480;

constexpr int kBacklightPin = 1;
constexpr int kCsPin = 45;
constexpr int kSckPin = 47;
constexpr int kData0Pin = 21;
constexpr int kData1Pin = 48;
constexpr int kData2Pin = 40;
constexpr int kData3Pin = 39;
constexpr int kTouchSda = 4;
constexpr int kTouchScl = 8;
constexpr int kTouchAddress = 0x3B;
constexpr int kBootPin = 0;

constexpr hal::BoardDescriptor kDescriptor = {
    "jc3248w535", "Guition JC3248W535", kWidth, kHeight, hal::Rotation::R270, hal::Rotation::R0,
};

// The AXS15231B only accepts whole-frame writes over QSPI, so the frame goes out in bands that are rotated and
// byte-swapped on the fly while the previous band is on the wire. Display + backlight are one chip path but
// expose separate ports so the core never couples them.
class Axs15231bDisplay final : public hal::DisplayPort, public hal::BacklightPort {
public:
    bool begin() override {
        gpio_hold_dis(static_cast<gpio_num_t>(kBacklightPin));
        gpio_hold_dis(static_cast<gpio_num_t>(kCsPin));
        ledcAttach(kBacklightPin, 20000, 8);
        ledcWrite(kBacklightPin, 255);
        bus_ = new Arduino_ESP32QSPI(kCsPin, kSckPin, kData0Pin, kData1Pin, kData2Pin, kData3Pin, true);
        panel_ = new Arduino_AXS15231B(bus_, GFX_NOT_DEFINED, 0, false, kWidth, kHeight, 0, 0, 0, 0,
                                       axs15231b_320480_type1_init_operations,
                                       sizeof(axs15231b_320480_type1_init_operations));
        if (!panel_->begin()) return false;
        return start_pixel_device();
    }

    void push(const uint16_t *frame, int16_t width, int16_t height, hal::Rotation rotation) override {
        if (!pixels_ || !frame) return;
        (void)width;
        (void)height;
        panel_->startWrite();
        panel_->writeAddrWindow(0, 0, kWidth, kHeight);
        panel_->endWrite();
        gpio_set_level(static_cast<gpio_num_t>(kCsPin), 0);
        for (int b = 0; b < kBands; ++b) {
            int slot = b & 1;
            if (b >= 2) {
                spi_transaction_t *done = nullptr;
                spi_device_get_trans_result(pixels_, &done, portMAX_DELAY);
            }
            fill_band(frame, band_[slot], b * kBandRows, rotation);
            spi_transaction_ext_t &t = trans_[slot];
            memset(&t, 0, sizeof(t));
            if (b == 0) {
                t.base.flags = SPI_TRANS_MODE_QIO;
                t.base.cmd = 0x32;
                t.base.addr = 0x003C00;
            } else {
                t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR |
                               SPI_TRANS_VARIABLE_DUMMY;
            }
            t.base.tx_buffer = band_[slot];
            t.base.length = kWidth * kBandRows * 16;
            spi_device_queue_trans(pixels_, &t.base, portMAX_DELAY);
        }
        for (int i = 0; i < 2; ++i) {
            spi_transaction_t *done = nullptr;
            spi_device_get_trans_result(pixels_, &done, portMAX_DELAY);
        }
        gpio_set_level(static_cast<gpio_num_t>(kCsPin), 1);
    }

    void set_brightness(uint8_t percent) override {
        if (percent > 100) percent = 100;
        ledcWrite(kBacklightPin, percent * 255 / 100);
    }

    // PWM stops at reset, so latch the pins at a steady level: backlight on, panel deselected.
    void hold_through_reset() override {
        ledcDetach(kBacklightPin);
        pinMode(kBacklightPin, OUTPUT);
        digitalWrite(kBacklightPin, HIGH);
        gpio_set_level(static_cast<gpio_num_t>(kCsPin), 1);
        gpio_hold_en(static_cast<gpio_num_t>(kBacklightPin));
        gpio_hold_en(static_cast<gpio_num_t>(kCsPin));
    }

private:
    static constexpr int kBandRows = 16;
    static constexpr int kBands = kHeight / kBandRows;

    bool start_pixel_device() {
        spi_device_interface_config_t config = {};
        config.command_bits = 8;
        config.address_bits = 24;
        config.mode = 0;
        config.clock_source = SPI_CLK_SRC_DEFAULT;
        config.clock_speed_hz = 40000000;
        config.spics_io_num = -1;
        config.flags = SPI_DEVICE_HALFDUPLEX;
        config.queue_size = 2;
        if (spi_bus_add_device(SPI2_HOST, &config, &pixels_) != ESP_OK) return false;
        for (uint16_t *&slot : band_) {
            slot = static_cast<uint16_t *>(
                heap_caps_aligned_alloc(16, kWidth * kBandRows * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
            if (!slot) return false;
        }
        return true;
    }

    // Native rows [y0, y0 + kBandRows) of the logical frame, big-endian RGB565.
    static void fill_band(const uint16_t *frame, uint16_t *out, int y0, hal::Rotation rotation) {
        switch (rotation) {
            case hal::Rotation::R270:
                for (int x = 0; x < kWidth; ++x) {
                    const uint16_t *src = frame + (kWidth - 1 - x) * kHeight + y0;
                    uint16_t *dst = out + x;
                    for (int r = 0; r < kBandRows; ++r) dst[r * kWidth] = __builtin_bswap16(src[r]);
                }
                break;
            case hal::Rotation::R90:
                for (int x = 0; x < kWidth; ++x) {
                    const uint16_t *src = frame + x * kHeight + (kHeight - 1 - y0);
                    uint16_t *dst = out + x;
                    for (int r = 0; r < kBandRows; ++r) dst[r * kWidth] = __builtin_bswap16(src[-r]);
                }
                break;
            case hal::Rotation::R180:
                for (int r = 0; r < kBandRows; ++r) {
                    const uint16_t *src = frame + (kHeight - 1 - y0 - r) * kWidth + kWidth - 1;
                    uint16_t *dst = out + r * kWidth;
                    for (int x = 0; x < kWidth; ++x) dst[x] = __builtin_bswap16(src[-x]);
                }
                break;
            default: {
                const uint16_t *src = frame + y0 * kWidth;
                for (int i = 0; i < kWidth * kBandRows; ++i) out[i] = __builtin_bswap16(src[i]);
            }
        }
    }

    Arduino_DataBus *bus_ = nullptr;
    Arduino_AXS15231B *panel_ = nullptr;
    spi_device_handle_t pixels_ = nullptr;
    uint16_t *band_[2] = {};
    spi_transaction_ext_t trans_[2];
};

class Axs15231bTouch final : public hal::TouchPort {
public:
    bool begin() override {
        Wire.begin(kTouchSda, kTouchScl);
        Wire.setClock(400000);
        return true;
    }

    hal::TouchRead read(int16_t &x, int16_t &y) override {
        static constexpr uint8_t kCommand[11] = {0xb5, 0xab, 0xa5, 0x5a, 0, 0, 0, 8, 0, 0, 0};
        Wire.beginTransmission(kTouchAddress);
        Wire.write(kCommand, sizeof(kCommand));
        if (Wire.endTransmission() != 0) return hal::TouchRead::Error;
        if (Wire.requestFrom(kTouchAddress, 8) != 8) return hal::TouchRead::Error;
        uint8_t data[8];
        for (uint8_t &byte : data) byte = Wire.read();
        if (data[1] == 0) return hal::TouchRead::None;
        if (data[1] > 5) return hal::TouchRead::Error;
        x = static_cast<int16_t>(((data[2] & 0x0f) << 8) | data[3]);
        y = static_cast<int16_t>(((data[4] & 0x0f) << 8) | data[5]);
        if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return hal::TouchRead::Error;
        return hal::TouchRead::Contact;
    }
};

class I2sSpeaker final : public hal::SoundPort {
public:
    bool begin(uint32_t sample_rate) override {
        i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        if (i2s_new_channel(&channel, &tx_, nullptr) != ESP_OK) {
            tx_ = nullptr;
            return false;
        }
        i2s_std_config_t config = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = GPIO_NUM_42,
                .ws = GPIO_NUM_2,
                .dout = GPIO_NUM_41,
                .din = I2S_GPIO_UNUSED,
            },
        };
        if (i2s_channel_init_std_mode(tx_, &config) != ESP_OK || i2s_channel_enable(tx_) != ESP_OK) {
            i2s_del_channel(tx_);
            tx_ = nullptr;
            return false;
        }
        return true;
    }

    size_t write(const int16_t *frames, size_t count) override {
        if (!tx_) return 0;
        size_t written = 0;
        if (i2s_channel_write(tx_, frames, count * 2 * sizeof(int16_t), &written, 0) != ESP_OK) return 0;
        return written / (2 * sizeof(int16_t));
    }

private:
    i2s_chan_handle_t tx_ = nullptr;
};

class BootButton final : public hal::ButtonPort {
public:
    bool begin() override {
        pinMode(kBootPin, INPUT_PULLUP);
        return true;
    }
    bool down() override { return digitalRead(kBootPin) == LOW; }
};

// GPIO5 ADC divider — see power.cpp historical notes. USB without a cell biases the sense line,
// so pack-detect stays false until a STAT / pack-detect line exists.
class JcPower final : public hal::PowerPort {
public:
    bool battery_present() override {
        static bool logged = false;
        int pin = pin_mv();
        if (!logged) {
            logged = true;
            Serial.printf("bateria pino=%d mV cell~=%d mV (ícone oculto: sem detecção confiável sem célula)\n", pin,
                          cell_mv_from_pin(pin));
        }
        (void)pin;
        return false;
    }

    int battery_percent() override {
        if (!battery_present()) return -1;
        int mv = cell_mv_from_pin(pin_mv());
        if (mv <= kEmptyMv) return 0;
        if (mv >= kFullMv) return 100;
        return (mv - kEmptyMv) * 100 / (kFullMv - kEmptyMv);
    }

private:
    static constexpr int kSensePin = 5;
    static constexpr int kSamples = 8;
    static constexpr int kEmptyMv = 3300;
    static constexpr int kFullMv = 4200;

    static int pin_mv() {
        analogSetPinAttenuation(kSensePin, ADC_11db);
        uint32_t sum = 0;
        for (int i = 0; i < kSamples; ++i) sum += analogReadMilliVolts(kSensePin);
        return static_cast<int>(sum / kSamples);
    }

    static int cell_mv_from_pin(int pin) { return pin * 133 / 100; }
};

class NvsStorage final : public hal::StoragePort {
public:
    bool get(const char *ns, const char *key, char *out, size_t out_len) override {
        if (!ns || !key || !out || out_len == 0) return false;
        Preferences prefs;
        if (!prefs.begin(ns, true)) return false;
        if (!prefs.isKey(key)) {
            prefs.end();
            out[0] = '\0';
            return false;
        }
        String value = prefs.getString(key, "");
        prefs.end();
        std::strncpy(out, value.c_str(), out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }

    bool set(const char *ns, const char *key, const char *value) override {
        if (!ns || !key || !value) return false;
        Preferences prefs;
        if (!prefs.begin(ns, false)) return false;
        prefs.putString(key, value);
        prefs.end();
        return true;
    }

    bool get_u32(const char *ns, const char *key, uint32_t *out) override {
        if (!ns || !key || !out) return false;
        Preferences prefs;
        if (!prefs.begin(ns, true)) return false;
        if (!prefs.isKey(key)) {
            prefs.end();
            return false;
        }
        // Legacy keys were putUChar; accept both so existing NVS keeps working.
        switch (prefs.getType(key)) {
            case PT_U8:
                *out = prefs.getUChar(key, 0);
                break;
            case PT_U16:
                *out = prefs.getUShort(key, 0);
                break;
            default:
                *out = prefs.getUInt(key, 0);
                break;
        }
        prefs.end();
        return true;
    }

    bool set_u32(const char *ns, const char *key, uint32_t value) override {
        if (!ns || !key) return false;
        Preferences prefs;
        if (!prefs.begin(ns, false)) return false;
        prefs.putUInt(key, value);
        prefs.end();
        return true;
    }

    bool get_bool(const char *ns, const char *key, bool *out) override {
        if (!ns || !key || !out) return false;
        Preferences prefs;
        if (!prefs.begin(ns, true)) return false;
        if (!prefs.isKey(key)) {
            prefs.end();
            return false;
        }
        *out = prefs.getBool(key, false);
        prefs.end();
        return true;
    }

    bool set_bool(const char *ns, const char *key, bool value) override {
        if (!ns || !key) return false;
        Preferences prefs;
        if (!prefs.begin(ns, false)) return false;
        prefs.putBool(key, value);
        prefs.end();
        return true;
    }

    bool get_f32(const char *ns, const char *key, float *out) override {
        if (!ns || !key || !out) return false;
        Preferences prefs;
        if (!prefs.begin(ns, true)) return false;
        if (!prefs.isKey(key)) {
            prefs.end();
            return false;
        }
        *out = prefs.getFloat(key, 0);
        prefs.end();
        return true;
    }

    bool set_f32(const char *ns, const char *key, float value) override {
        if (!ns || !key) return false;
        Preferences prefs;
        if (!prefs.begin(ns, false)) return false;
        prefs.putFloat(key, value);
        prefs.end();
        return true;
    }

    bool erase(const char *ns, const char *key) override {
        if (!ns || !key) return false;
        Preferences prefs;
        if (!prefs.begin(ns, false)) return false;
        prefs.remove(key);
        prefs.end();
        return true;
    }

    bool clear_ns(const char *ns) override {
        if (!ns) return false;
        Preferences prefs;
        if (!prefs.begin(ns, false)) return false;
        prefs.clear();
        prefs.end();
        return true;
    }
};

constexpr hal::Capabilities kCapabilities = {
    /*display*/ true, /*touch*/ true, /*backlight*/ true, /*battery*/ false,
    /*sound*/ true,   /*button*/ true, /*storage*/ true,
};

Axs15231bDisplay display;
Axs15231bTouch touch;
I2sSpeaker speaker;
BootButton button;
JcPower power;
NvsStorage storage;
hal::Board instance = {kDescriptor, &display, &touch, &display, &power, &speaker, &button, &storage, kCapabilities};
}  // namespace

hal::Board &hal::board() { return instance; }
