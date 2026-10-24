#pragma once

#include <stddef.h>
#include <stdint.h>

// Storage: microSD (SDMMC 1-bit CLK12/CMD11/D0 13) com fallback FFat na flash.
namespace sd_host {

// Monta SD se possível; senão FFat. Idempotente.
bool begin();
// Garante volume (SD preferido, senão flash). Leve — ok no portal.
bool ensure();
// Remonta SD; se falhar, mantém/volta para flash.
bool remount();
bool ready();
// No loop: se boot ficou só em flash, tenta SD por um tempo.
void poll();

// Free / total bytes on the volume, or 0 if not mounted.
uint64_t free_bytes();
uint64_t total_bytes();
// "SD", "FFat", or "" when nothing is mounted.
const char *backend_name();
// SPIRAM buffer helpers (widgets must not include esp_heap / Arduino).
uint8_t *alloc_spiram(size_t bytes);
void free_buf(void *p);
void pause_ms(uint32_t ms);
// Read entire file into a SPIRAM buffer. Caller frees with free_buf.
uint8_t *read_file(const char *path, size_t *size);
bool write_file(const char *path, const uint8_t *data, size_t size);
bool remove_file(const char *path);
bool exists(const char *path);
bool mkdir_p(const char *path);
void describe(char *dest, size_t cap);
void list_dir(const char *path);

// Stream read/write (não carrega o arquivo inteiro na RAM). Um arquivo aberto por vez.
bool open_read(const char *path, size_t *size);
size_t read(uint8_t *buf, size_t n);
void close_read();

bool open_write(const char *path);
size_t write(const uint8_t *buf, size_t n);
// Fecha e confere se gravou `expect` bytes (0 = só fechar).
bool close_write(size_t expect = 0);

}  // namespace sd_host
