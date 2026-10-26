#pragma once

#include <stddef.h>
#include <stdint.h>

// Widget-scoped settings behind StoragePort (NVS). Keys are short (NVS ≤15);
// callers pass Tessera instance_id + logical key; mapping is internal.
namespace widget_settings {

bool get(const char *instance_id, const char *key, char *out, size_t out_len);
bool set(const char *instance_id, const char *key, const char *value);

uint8_t get_u8(const char *instance_id, const char *key, uint8_t fallback);
bool set_u8(const char *instance_id, const char *key, uint8_t value);

bool get_bool(const char *instance_id, const char *key, bool fallback);
bool set_bool(const char *instance_id, const char *key, bool value);

// Convenience — org.tessera.todoist / sync_minutes
uint8_t todoist_sync_minutes(uint8_t fallback);
void set_todoist_sync_minutes(uint8_t minutes);

}  // namespace widget_settings
