#include "ui/weather_icons.h"

#include "platform/log.h"
#include "platform/sd_host.h"
#include "ui/weather_icons_data.h"

#include <cstring>

namespace {
struct Icon {
    lv_image_dsc_t dsc;
    uint8_t *owned = nullptr;  // SPIRAM when loaded from SD
};

Icon clear_day;
Icon partly;
Icon overcast;
Icon fog;
Icon rain;
Icon snow;
Icon storm;
Icon empty;
bool ready = false;
bool from_sd = false;

void bind_flash(Icon &icon, const uint8_t *data, size_t bytes) {
    if (icon.owned) {
        sd_host::free_buf(icon.owned);
        icon.owned = nullptr;
    }
    icon.dsc = {};
    icon.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon.dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    icon.dsc.header.w = static_cast<uint32_t>(weather_icons_data::side());
    icon.dsc.header.h = static_cast<uint32_t>(weather_icons_data::side());
    icon.dsc.header.stride = static_cast<uint32_t>(weather_icons_data::side() * 2);
    icon.dsc.data_size = bytes;
    icon.dsc.data = data;
}

bool bind_wxia(Icon &icon, const char *path) {
    size_t bytes = 0;
    uint8_t *raw = sd_host::read_file(path, &bytes);
    if (!raw) {
        logger::printf("sd lendo %s falhou\n", path);
        return false;
    }
    if (bytes < 8 || memcmp(raw, "WXIA", 4) != 0) {
        logger::printf("sd %s magia ruim (%u) %02x %02x %02x %02x\n", path, static_cast<unsigned>(bytes),
                       raw[0], raw[1], raw[2], raw[3]);
        sd_host::free_buf(raw);
        return false;
    }
    uint16_t w = static_cast<uint16_t>(raw[4] | (raw[5] << 8));
    uint16_t h = static_cast<uint16_t>(raw[6] | (raw[7] << 8));
    size_t need = 8u + static_cast<size_t>(w) * h * 3u;
    if (w == 0 || h == 0 || bytes < need) {
        logger::printf("sd %s tamanho ruim %ux%u need=%u got=%u\n", path, w, h, static_cast<unsigned>(need),
                       static_cast<unsigned>(bytes));
        sd_host::free_buf(raw);
        return false;
    }
    if (icon.owned) sd_host::free_buf(icon.owned);
    icon.owned = raw;
    icon.dsc = {};
    icon.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon.dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    icon.dsc.header.w = w;
    icon.dsc.header.h = h;
    icon.dsc.header.stride = static_cast<uint32_t>(w * 2);
    icon.dsc.data_size = need - 8;
    icon.dsc.data = raw + 8;
    return true;
}

// Own folder so we don't clash with existing /weather/*.mjpeg on the card.
constexpr char kClear[] = "/tessera/clear.wxia";
constexpr char kPartly[] = "/tessera/partly.wxia";
constexpr char kOvercast[] = "/tessera/overcast.wxia";
constexpr char kFog[] = "/tessera/fog.wxia";
constexpr char kRain[] = "/tessera/rain.wxia";
constexpr char kSnow[] = "/tessera/snow.wxia";
constexpr char kStorm[] = "/tessera/storm.wxia";
constexpr char kEmpty[] = "/tessera/empty.wxia";

bool wxia_ok(const char *path) {
    // Cheap header probe — avoid loading full RGB565A8 just to decide whether to seed.
    size_t n = 0;
    uint8_t *probe = sd_host::read_file(path, &n);
    bool good = false;
    if (probe && n >= 8 && memcmp(probe, "WXIA", 4) == 0) {
        uint16_t w = static_cast<uint16_t>(probe[4] | (probe[5] << 8));
        uint16_t h = static_cast<uint16_t>(probe[6] | (probe[7] << 8));
        size_t need = 8u + static_cast<size_t>(w) * h * 3u;
        good = w > 0 && h > 0 && n >= need;
    }
    if (probe) sd_host::free_buf(probe);
    return good;
}

void seed_one(const char *path, const uint8_t *flash, size_t flash_bytes, int side) {
    if (wxia_ok(path)) return;
    sd_host::remove_file(path);
    size_t total = 8 + flash_bytes;
    uint8_t *buf = sd_host::alloc_spiram(total);
    if (!buf) return;
    buf[0] = 'W';
    buf[1] = 'X';
    buf[2] = 'I';
    buf[3] = 'A';
    buf[4] = static_cast<uint8_t>(side & 0xFF);
    buf[5] = static_cast<uint8_t>((side >> 8) & 0xFF);
    buf[6] = static_cast<uint8_t>(side & 0xFF);
    buf[7] = static_cast<uint8_t>((side >> 8) & 0xFF);
    memcpy(buf + 8, flash, flash_bytes);
    bool ok = sd_host::write_file(path, buf, total);
    sd_host::free_buf(buf);
    logger::printf("sd seed %s %s\n", path, ok ? "ok" : "falhou");
    sd_host::pause_ms(15);
}

void seed_sd_from_flash() {
    if (!sd_host::ready()) return;
    sd_host::mkdir_p("/tessera");
    size_t n = 0;
    int side = weather_icons_data::side();
    seed_one(kClear, weather_icons_data::clear_day(&n), n, side);
    seed_one(kPartly, weather_icons_data::partly_cloudy(&n), n, side);
    seed_one(kOvercast, weather_icons_data::overcast(&n), n, side);
    seed_one(kFog, weather_icons_data::fog(&n), n, side);
    seed_one(kRain, weather_icons_data::rain(&n), n, side);
    seed_one(kSnow, weather_icons_data::snow(&n), n, side);
    seed_one(kStorm, weather_icons_data::thunderstorms(&n), n, side);
    seed_one(kEmpty, weather_icons_data::unavailable(&n), n, side);
}

void load_flash() {
    size_t n = 0;
    bind_flash(clear_day, weather_icons_data::clear_day(&n), n);
    bind_flash(partly, weather_icons_data::partly_cloudy(&n), n);
    bind_flash(overcast, weather_icons_data::overcast(&n), n);
    bind_flash(fog, weather_icons_data::fog(&n), n);
    bind_flash(rain, weather_icons_data::rain(&n), n);
    bind_flash(snow, weather_icons_data::snow(&n), n);
    bind_flash(storm, weather_icons_data::thunderstorms(&n), n);
    bind_flash(empty, weather_icons_data::unavailable(&n), n);
}

bool load_one(Icon &icon, const char *path, const uint8_t *(*flash_fn)(size_t *)) {
    if (bind_wxia(icon, path)) return true;
    size_t n = 0;
    bind_flash(icon, flash_fn(&n), n);
    return false;
}

bool load_sd() {
    if (!sd_host::ready()) return false;
    seed_sd_from_flash();
    int from = 0;
    if (load_one(clear_day, kClear, weather_icons_data::clear_day)) ++from;
    if (load_one(partly, kPartly, weather_icons_data::partly_cloudy)) ++from;
    if (load_one(overcast, kOvercast, weather_icons_data::overcast)) ++from;
    if (load_one(fog, kFog, weather_icons_data::fog)) ++from;
    if (load_one(rain, kRain, weather_icons_data::rain)) ++from;
    if (load_one(snow, kSnow, weather_icons_data::snow)) ++from;
    if (load_one(storm, kStorm, weather_icons_data::thunderstorms)) ++from;
    if (load_one(empty, kEmpty, weather_icons_data::unavailable)) ++from;
    logger::printf("clima icones sd %d/8\n", from);
    return from > 0;
}

void ensure() {
    if (ready) return;
    if (load_sd()) {
        from_sd = true;
        logger::printf("clima icones do sd (%d px)\n", clear_day.dsc.header.w);
    } else {
        load_flash();
        from_sd = false;
        logger::printf("clima icones da flash (%d px)\n", weather_icons_data::side());
    }
    ready = true;
}
}  // namespace

const lv_image_dsc_t *weather_icons::for_code(int code) {
    ensure();
    switch (code) {
        case 0:
        case 1: return &clear_day.dsc;
        case 2: return &partly.dsc;
        case 3: return &overcast.dsc;
        case 45:
        case 48: return &fog.dsc;
        case 51:
        case 53:
        case 55:
        case 56:
        case 57:
        case 61:
        case 63:
        case 65:
        case 66:
        case 67:
        case 80:
        case 81:
        case 82: return &rain.dsc;
        case 71:
        case 73:
        case 75:
        case 77:
        case 85:
        case 86: return &snow.dsc;
        case 95:
        case 96:
        case 99: return &storm.dsc;
        default: return &empty.dsc;
    }
}

const lv_image_dsc_t *weather_icons::unavailable() {
    ensure();
    return &empty.dsc;
}

void weather_icons::apply_theme() {
    // Prefer SD assets when the card is present (also after a late remount via SD).
    if (sd_host::ready() && !from_sd) {
        ready = false;
        ensure();
    }
}
