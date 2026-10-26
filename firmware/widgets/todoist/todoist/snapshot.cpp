#include "todoist/snapshot.h"

#include <cstdio>

todoist::Summary todoist::summarize(const ListTally *rows, int count) {
    Summary summary = {};
    if (!rows || count <= 0) return summary;
    for (int i = 0; i < count; ++i) {
        if (!rows[i].shown) continue;
        if (!rows[i].ready) {
            summary.pending = true;
            continue;
        }
        if (rows[i].count > 0) summary.total += rows[i].count;
    }
    return summary;
}

void todoist::format_home_count(const Summary &summary, char *out, size_t cap) {
    if (!out || cap == 0) return;
    // Only show "…" when nothing is Ready yet. Partial totals beat an empty tile
    // while one list is still warming (agent USB / Wi‑Fi).
    if (summary.pending && summary.total <= 0) {
        snprintf(out, cap, "…");
        return;
    }
    snprintf(out, cap, "%d", summary.total);
}

uint32_t todoist::stamp(const Gate &gate) { return gate.generation; }

void todoist::bump(Gate &gate) { gate.generation++; }

bool todoist::accept(const Gate &gate, uint32_t response_generation) {
    return response_generation == gate.generation;
}
