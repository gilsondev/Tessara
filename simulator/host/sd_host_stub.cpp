#include "platform/sd_host.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Host stub: no SD/FFat — weather_icons falls back to flash assets.
namespace sd_host {

bool begin() { return false; }
bool ensure() { return false; }
bool remount() { return false; }
bool ready() { return false; }
void poll() {}
uint64_t free_bytes() { return 0; }
uint8_t *alloc_spiram(size_t bytes) { return static_cast<uint8_t *>(std::malloc(bytes)); }
void free_buf(void *p) { std::free(p); }
void pause_ms(uint32_t) {}
uint8_t *read_file(const char *, size_t *size) {
    if (size) *size = 0;
    return nullptr;
}
bool write_file(const char *, const uint8_t *, size_t) { return false; }
bool remove_file(const char *) { return false; }
bool exists(const char *) { return false; }
bool mkdir_p(const char *) { return false; }
void describe(char *dest, size_t cap) {
    if (dest && cap) std::snprintf(dest, cap, "sim:no-sd");
}
void list_dir(const char *) {}
bool open_read(const char *, size_t *size) {
    if (size) *size = 0;
    return false;
}
size_t read(uint8_t *, size_t) { return 0; }
void close_read() {}
bool open_write(const char *) { return false; }
size_t write(const uint8_t *, size_t) { return 0; }
bool close_write(size_t) { return false; }

}  // namespace sd_host
