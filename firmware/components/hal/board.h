#pragma once

#include <stddef.h>
#include <stdint.h>

// Portable ports a board adapter implements. Everything above this layer is hardware agnostic;
// a board is chosen at build time by compiling exactly one directory under firmware/boards/.
// Code dependencies point at these interfaces — never at a specific board implementation.
namespace hal {

enum class Rotation : uint8_t { R0, R90, R180, R270 };

struct BoardDescriptor {
    const char *id;
    const char *name;
    int16_t width;   // native panel columns
    int16_t height;  // native panel rows
    Rotation landscape;
    Rotation portrait;
};

// What this board actually exposes (diagram: Capabilities).
struct Capabilities {
    bool display = false;
    bool touch = false;
    bool backlight = false;
    bool battery = false;
    bool sound = false;
    bool button = false;
    bool storage = false;
};

// Receives whole logical frames (RGB565, row-major) and owns the panel's scan order.
class DisplayPort {
public:
    virtual ~DisplayPort() = default;
    virtual bool begin() = 0;
    virtual void push(const uint16_t *frame, int16_t width, int16_t height, Rotation rotation) = 0;
};

// PWM / hold for the panel lamp (diagram: Backlight). Separate from DisplayPort.
class BacklightPort {
public:
    virtual ~BacklightPort() = default;
    // 0 turns the backlight off, 100 is full brightness.
    virtual void set_brightness(uint8_t percent) = 0;
    // Keep the current picture lit through a software reset (e.g. into the USB bootloader).
    virtual void hold_through_reset() {}
};

enum class TouchRead : uint8_t { None, Contact, Error };

class TouchPort {
public:
    virtual ~TouchPort() = default;
    virtual bool begin() = 0;
    // Native panel coordinates.
    virtual TouchRead read(int16_t &x, int16_t &y) = 0;
};

// Diagram name for touch / pointer input.
using InputPort = TouchPort;

class SoundPort {
public:
    virtual ~SoundPort() = default;
    virtual bool begin(uint32_t sample_rate) = 0;
    // Non-blocking; interleaved stereo frames. Returns frames accepted.
    virtual size_t write(const int16_t *frames, size_t count) = 0;
};

class ButtonPort {
public:
    virtual ~ButtonPort() = default;
    virtual bool begin() = 0;
    virtual bool down() = 0;
};

// Diagram: Power — pack presence / charge estimate (or -1 when unknown).
class PowerPort {
public:
    virtual ~PowerPort() = default;
    virtual bool battery_present() = 0;
    // 0–100 when a pack is present, otherwise −1.
    virtual int battery_percent() = 0;
};

// Diagram: Storage — namespaced key/value (NVS / host file). Callers use this, never Preferences.
class StoragePort {
public:
    virtual ~StoragePort() = default;
    virtual bool get(const char *ns, const char *key, char *out, size_t out_len) = 0;
    virtual bool set(const char *ns, const char *key, const char *value) = 0;
    virtual bool get_u32(const char *ns, const char *key, uint32_t *out) = 0;
    virtual bool set_u32(const char *ns, const char *key, uint32_t value) = 0;
    virtual bool get_bool(const char *ns, const char *key, bool *out) = 0;
    virtual bool set_bool(const char *ns, const char *key, bool value) = 0;
    virtual bool get_f32(const char *ns, const char *key, float *out) = 0;
    virtual bool set_f32(const char *ns, const char *key, float value) = 0;
    virtual bool erase(const char *ns, const char *key) = 0;
    virtual bool clear_ns(const char *ns) = 0;
};

struct Board {
    const BoardDescriptor &descriptor;
    DisplayPort *display;
    InputPort *touch;  // InputPort
    BacklightPort *backlight;
    PowerPort *power;
    SoundPort *sound;
    ButtonPort *button;
    StoragePort *storage;
    Capabilities capabilities;
};

Board &board();

}  // namespace hal
