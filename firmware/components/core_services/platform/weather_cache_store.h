#pragma once

#include <stddef.h>

// Last forecast, kept apart from the user's widget settings.
namespace weather_cache_store {

bool load(char *out, size_t out_len);
bool save(const char *blob);
// Moves a forecast that was stored as a widget setting into this cache, then deletes it there.
void migrate_from_settings();

}  // namespace weather_cache_store
