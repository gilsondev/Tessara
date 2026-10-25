#pragma once

#include <stddef.h>

// Ordered list of enabled widget type IDs for the home (Tessara dashboard.enabled spirit).
namespace dashboard {

constexpr int kMaxSlots = 8;
constexpr int kIdLen = 48;

struct Layout {
    char ids[kMaxSlots][kIdLen];
    int count;
};

// Home defaults: hero (saudação/data/relógio) + Tarefas, Clima, AI Tokens.
Layout defaults();

// Known catalog (extra types may be registered later; unknown CSV tokens are ignored).
bool known_type(const char *type_id);

// CSV: "org.tessara.todoist,org.tessara.weather" — empty / garbage → defaults().
bool parse(const char *csv, Layout &out);
void format(const Layout &in, char *dest, size_t cap);

bool is_enabled(const Layout &layout, const char *type_id);
// Enable appends at end if missing; disable removes. Unknown type_id is a no-op.
void set_enabled(Layout &layout, const char *type_id, bool on);

// Home slots = enabled ∩ registered (order preserved). Skips ids not in registry.
int compose(const Layout &enabled, const char *const *registered, int registered_n, char out[][kIdLen],
            int out_cap);

// Canonical short ids used by the lab (also match widget manifests when present).
const char *id_todoist();
const char *id_weather();
const char *id_aitokens();
const char *id_calendar();
const char *id_clock();
const char *id_greeting();
const char *id_date();
// Legacy CSV only — not in known_type.
const char *id_hello();

}  // namespace dashboard
