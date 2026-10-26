#pragma once

#include <stddef.h>

// Ordered list of enabled widget type IDs for the home (Tessera dashboard.enabled spirit).
namespace dashboard {

constexpr int kMaxSlots = 10;
constexpr int kIdLen = 48;

struct Layout {
    char ids[kMaxSlots][kIdLen];
    int count;
};

// Home defaults: hero + Tarefas, Clima, AI Tokens, Agenda, Música, Mac.
Layout defaults();

// Known catalog (extra types may be registered later; unknown CSV tokens are ignored).
bool known_type(const char *type_id);

// CSV: "org.tessera.todoist,org.tessera.weather" — empty / garbage → defaults().
// Also accepts legacy org.tessara.* ids and rewrites them to org.tessera.*.
bool parse(const char *csv, Layout &out);
void format(const Layout &in, char *dest, size_t cap);

bool is_enabled(const Layout &layout, const char *type_id);
// Enable appends at end if missing; disable removes. Unknown type_id is a no-op.
void set_enabled(Layout &layout, const char *type_id, bool on);
// Old NVS layouts predate Música/Mac — append them once. True if layout changed.
bool ensure_media_mac(Layout &layout);
// Brand-rename recovery: layouts that lost Agenda when org.tessara.* was dropped.
// True if calendar was appended. Callers should gate with a one-shot NVS flag so a
// later intentional hide still sticks.
bool ensure_calendar(Layout &layout);

// Home slots = enabled ∩ registered (order preserved). Skips ids not in registry.
int compose(const Layout &enabled, const char *const *registered, int registered_n, char out[][kIdLen],
            int out_cap);

// Canonical short ids used by the lab (also match widget manifests when present).
const char *id_todoist();
const char *id_weather();
const char *id_aitokens();
const char *id_calendar();
const char *id_media();
const char *id_mac();
const char *id_clock();
const char *id_greeting();
const char *id_date();
// Legacy CSV only — not in known_type.
const char *id_hello();

}  // namespace dashboard
