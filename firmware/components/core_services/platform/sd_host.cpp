#include "platform/sd_host.h"

#include "platform/log.h"

#include <Arduino.h>
#include <FFat.h>
#include <FS.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>

namespace {
// Guition JC3248W535 microSD: SDMMC 1-bit CLK=12 CMD=11 D0=13; CS/D3=10 idle HIGH.
// Se o cartão não responder (case/contato), usa partição FFat na flash.
constexpr int kClk = 12;
constexpr int kCmd = 11;
constexpr int kD0 = 13;
constexpr int kCs = 10;
constexpr uint32_t kPollEveryMs = 3000;
constexpr uint32_t kPollWindowMs = 120000;

enum class Backend : uint8_t { None, Sd, Flash };

Backend backend = Backend::None;
bool sd_pins_set = false;
bool flash_ready = false;
bool poll_armed = false;
uint32_t poll_started_ms = 0;
uint32_t poll_next_ms = 0;

File stream_file;
bool stream_open = false;
bool stream_writing = false;
size_t stream_wrote = 0;
char stream_path[96] = {};

fs::FS *volume() {
    if (backend == Backend::Sd) return &SD_MMC;
    if (backend == Backend::Flash) return &FFat;
    return nullptr;
}

const char *backend_name() {
    if (backend == Backend::Sd) return "sd";
    if (backend == Backend::Flash) return "flash";
    return "none";
}

void arm_poll() {
    poll_armed = true;
    poll_started_ms = millis();
    poll_next_ms = poll_started_ms + kPollEveryMs;
}

void hold_cs_idle() {
    pinMode(kCs, OUTPUT);
    digitalWrite(kCs, HIGH);
}

bool sd_probe_ok() {
    if (backend != Backend::Sd) return false;
    uint8_t type = SD_MMC.cardType();
    return type == CARD_SD || type == CARD_SDHC || type == CARD_MMC;
}

void close_stream_quiet() {
    if (!stream_open) return;
    stream_file.close();
    stream_open = false;
    stream_writing = false;
    stream_wrote = 0;
}

void unmount_flash() {
    if (!flash_ready) return;
    close_stream_quiet();
    FFat.end();
    flash_ready = false;
    if (backend == Backend::Flash) backend = Backend::None;
}

void unmount_sd() {
    close_stream_quiet();
    if (backend == Backend::Sd) {
        SD_MMC.end();
        backend = Backend::None;
    }
}

bool mount_flash() {
    if (backend == Backend::Sd) return false;  // SD ativo — não remonta FFat em paralelo
    if (flash_ready) {
        if (backend == Backend::None) backend = Backend::Flash;
        return true;
    }
    // formatIfFailed=true: 1ª vez após criar a partição.
    if (!FFat.begin(true, "/ffat")) {
        logger::printf("flash fat falhou\n");
        return false;
    }
    flash_ready = true;
    backend = Backend::Flash;
    logger::printf("storage ok via=flash size=%u KB free=%u KB\n",
                   static_cast<unsigned>(FFat.totalBytes() / 1024),
                   static_cast<unsigned>(FFat.freeBytes() / 1024));
    return true;
}

bool mount_sd_once(int freq_khz) {
    hold_cs_idle();
    if (!sd_pins_set) {
        if (!SD_MMC.setPins(kClk, kCmd, kD0)) return false;
        sd_pins_set = true;
    }
    if (backend == Backend::Sd) {
        SD_MMC.end();
        backend = Backend::None;
    }
    // Arduino SD_MMC.totalBytes()/usedBytes() always query FatFS drive "0:".
    // If FFat is also mounted it steals drive 0 and we report the 2 MB flash
    // partition as if it were the microSD — unmount FFat before SD.
    unmount_flash();
    if (!SD_MMC.begin("/sd", true, false, freq_khz)) return false;
    backend = Backend::Sd;
    if (!sd_probe_ok()) {
        SD_MMC.end();
        backend = Backend::None;
        return false;
    }
    return true;
}

bool try_sd() {
    if (mount_sd_once(SDMMC_FREQ_PROBING)) return true;
    delay(30);
    if (mount_sd_once(SDMMC_FREQ_DEFAULT)) return true;
    return false;
}

void log_sd_ok() {
    logger::printf("storage ok via=sd size=%llu MB free~=%llu MB\n",
                   static_cast<unsigned long long>(SD_MMC.cardSize() / (1024 * 1024)),
                   static_cast<unsigned long long>(sd_host::free_bytes() / (1024 * 1024)));
    poll_armed = false;
}
}  // namespace

bool sd_host::open_read(const char *path, size_t *size) {
    if (size) *size = 0;
    close_read();
    close_write(0);
    if (!ensure() || !path) return false;
    auto *vol = volume();
    if (!vol) return false;
    stream_file = vol->open(path, FILE_READ);
    if (!stream_file) return false;
    stream_open = true;
    stream_writing = false;
    if (size) *size = stream_file.size();
    return true;
}

size_t sd_host::read(uint8_t *buf, size_t n) {
    if (!stream_open || stream_writing || !buf || !n) return 0;
    return stream_file.read(buf, n);
}

void sd_host::close_read() {
    if (!stream_open || stream_writing) return;
    stream_file.close();
    stream_open = false;
}

bool sd_host::open_write(const char *path) {
    close_read();
    close_write(0);
    if (!ensure() || !path || !path[0]) return false;
    auto *vol = volume();
    if (!vol) return false;
    if (vol->exists(path)) vol->remove(path);
    stream_file = vol->open(path, FILE_WRITE);
    if (!stream_file) {
        logger::printf("storage open write falhou %s via=%s\n", path, backend_name());
        return false;
    }
    stream_open = true;
    stream_writing = true;
    stream_wrote = 0;
    strncpy(stream_path, path, sizeof(stream_path) - 1);
    stream_path[sizeof(stream_path) - 1] = '\0';
    return true;
}

size_t sd_host::write(const uint8_t *buf, size_t n) {
    if (!stream_open || !stream_writing || !buf || !n) return 0;
    size_t got = stream_file.write(buf, n);
    stream_wrote += got;
    return got;
}

bool sd_host::close_write(size_t expect) {
    if (!stream_open || !stream_writing) return true;
    stream_file.flush();
    stream_file.close();
    stream_open = false;
    stream_writing = false;
    size_t wrote = stream_wrote;
    stream_wrote = 0;
    if (expect == 0) return true;
    auto *vol = volume();
    size_t on_disk = 0;
    if (vol) {
        File check = vol->open(stream_path, FILE_READ);
        on_disk = check ? check.size() : 0;
        if (check) check.close();
    }
    if (wrote != expect || on_disk != expect) {
        logger::printf("storage write confere falhou %s wrote=%u disk=%u expect=%u\n", stream_path,
                       static_cast<unsigned>(wrote), static_cast<unsigned>(on_disk),
                       static_cast<unsigned>(expect));
        return false;
    }
    return true;
}

bool sd_host::begin() {
    Serial.println("storage: begin");
    Serial.flush();
    // Prefer microSD. FFat is only a fallback — never keep both mounted
    // (SD_MMC size APIs hit FatFS drive "0:" and would report the flash volume).
    if (try_sd()) {
        log_sd_ok();
        return true;
    }
    if (mount_flash()) {
        logger::printf("storage: sd ausente — usando flash\n");
        arm_poll();  // se o cartão aparecer depois, migra para SD
        return true;
    }
    logger::printf("storage: sd e flash falharam\n");
    arm_poll();
    return false;
}

bool sd_host::ensure() {
    if (backend == Backend::Sd && sd_probe_ok()) return true;
    if (backend == Backend::Flash && flash_ready) return true;
    if (try_sd()) {
        log_sd_ok();
        return true;
    }
    if (mount_flash()) {
        backend = Backend::Flash;
        return true;
    }
    arm_poll();
    return false;
}

bool sd_host::remount() {
    close_stream_quiet();
    unmount_sd();
    unmount_flash();
    sd_pins_set = false;
    delay(50);
    if (try_sd()) {
        log_sd_ok();
        return true;
    }
    if (mount_flash()) {
        logger::printf("storage: remount caiu em flash\n");
        return true;
    }
    logger::printf("storage ausente\n");
    arm_poll();
    return false;
}

void sd_host::poll() {
    if (!poll_armed) return;
    if (backend == Backend::Sd && sd_probe_ok()) {
        poll_armed = false;
        return;
    }
    const uint32_t now = millis();
    if (now - poll_started_ms > kPollWindowMs) {
        poll_armed = false;
        return;
    }
    if (now < poll_next_ms) return;
    poll_next_ms = now + kPollEveryMs;
    // Só tenta SD em background; flash já cobre o dia a dia.
    if (backend != Backend::Sd && try_sd()) {
        logger::printf("storage: sd montou em background\n");
        log_sd_ok();
    }
}

bool sd_host::ready() {
    if (backend == Backend::Sd) return sd_probe_ok();
    if (backend == Backend::Flash) return flash_ready;
    return false;
}

uint64_t sd_host::free_bytes() {
    if (!ready()) return 0;
    if (backend == Backend::Sd) {
        // Prefer card capacity − used; totalBytes() is the FAT volume (usually ≈ card).
        uint64_t total = SD_MMC.cardSize();
        if (!total) total = SD_MMC.totalBytes();
        uint64_t used = SD_MMC.usedBytes();
        return total > used ? total - used : 0;
    }
    return FFat.freeBytes();
}

uint64_t sd_host::total_bytes() {
    if (!ready()) return 0;
    if (backend == Backend::Sd) {
        uint64_t card = SD_MMC.cardSize();
        if (card) return card;
        return SD_MMC.totalBytes();
    }
    return FFat.totalBytes();
}

const char *sd_host::backend_name() {
    if (backend == Backend::Sd) return "Cartão SD";
    if (backend == Backend::Flash) return "Flash";
    return "";
}

bool sd_host::exists(const char *path) {
    auto *vol = volume();
    return ready() && path && vol && vol->exists(path);
}

bool sd_host::mkdir_p(const char *path) {
    if (!ensure() || !path || !path[0]) return false;
    auto *vol = volume();
    if (!vol) return false;
    if (vol->exists(path)) return true;
    char tmp[96];
    size_t n = 0;
    tmp[0] = '\0';
    for (const char *p = path; *p && n + 1 < sizeof(tmp); ++p) {
        if (*p == '/' && n > 0) {
            tmp[n] = '\0';
            if (!vol->exists(tmp) && !vol->mkdir(tmp)) return false;
        }
        tmp[n++] = *p;
    }
    tmp[n] = '\0';
    return vol->exists(tmp) || vol->mkdir(tmp);
}

uint8_t *sd_host::alloc_spiram(size_t bytes) {
    size_t n = bytes ? bytes : 1;
    return static_cast<uint8_t *>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void sd_host::free_buf(void *p) {
    if (p) heap_caps_free(p);
}

void sd_host::pause_ms(uint32_t ms) { delay(ms); }

uint8_t *sd_host::read_file(const char *path, size_t *size) {
    if (size) *size = 0;
    if (!ensure() || !path) return nullptr;
    auto *vol = volume();
    if (!vol) return nullptr;
    File file = vol->open(path, FILE_READ);
    if (!file) return nullptr;
    size_t bytes = file.size();
    uint8_t *buf = alloc_spiram(bytes);
    if (!buf) {
        file.close();
        return nullptr;
    }
    size_t got = file.read(buf, bytes);
    file.close();
    if (got != bytes) {
        free_buf(buf);
        return nullptr;
    }
    if (size) *size = bytes;
    return buf;
}

bool sd_host::write_file(const char *path, const uint8_t *data, size_t size) {
    if (!ensure() || !path || !data) return false;
    auto *vol = volume();
    if (!vol) return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (vol->exists(path)) vol->remove(path);
        File file = vol->open(path, FILE_WRITE);
        if (!file) {
            logger::printf("storage open write falhou %s\n", path);
            delay(40);
            continue;
        }
        size_t wrote = 0;
        while (wrote < size) {
            size_t chunk = size - wrote;
            if (chunk > 2048) chunk = 2048;
            size_t n = file.write(data + wrote, chunk);
            if (n == 0) break;
            wrote += n;
        }
        file.flush();
        file.close();
        if (wrote == size) {
            File check = vol->open(path, FILE_READ);
            size_t on_disk = check ? check.size() : 0;
            if (check) check.close();
            if (on_disk == size) return true;
        }
        logger::printf("storage write parcial %s %u/%u\n", path, static_cast<unsigned>(wrote),
                       static_cast<unsigned>(size));
        delay(40);
    }
    return false;
}

bool sd_host::remove_file(const char *path) {
    auto *vol = volume();
    return ready() && path && vol && vol->remove(path);
}

void sd_host::describe(char *dest, size_t cap) {
    if (!dest || !cap) return;
    if (!ready()) {
        snprintf(dest, cap, "storage: ausente");
        return;
    }
    snprintf(dest, cap, "%s: %llu KB livres", backend_name(),
             static_cast<unsigned long long>(free_bytes() / 1024));
}

void sd_host::list_dir(const char *path) {
    if (!ensure() || !path) {
        logger::printf("storage list: sem volume\n");
        return;
    }
    auto *vol = volume();
    if (!vol) return;
    File dir = vol->open(path);
    if (!dir || !dir.isDirectory()) {
        logger::printf("storage list: %s ausente\n", path);
        return;
    }
    logger::printf("storage(%s) %s:\n", backend_name(), path);
    for (;;) {
        File entry = dir.openNextFile();
        if (!entry) break;
        logger::printf("  %s (%u)\n", entry.name(), static_cast<unsigned>(entry.size()));
        entry.close();
    }
    dir.close();
}
