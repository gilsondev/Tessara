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
    // A partial total reads as "these are all the tasks". Keep the label blank
    // until every visible list is ready; the tile spinner is the loading state.
    if (summary.pending) {
        out[0] = '\0';
        return;
    }
    snprintf(out, cap, "%d", summary.total);
}

uint32_t todoist::stamp(const Gate &gate) { return gate.generation; }

void todoist::bump(Gate &gate) { gate.generation++; }

bool todoist::accept(const Gate &gate, uint32_t response_generation) {
    return response_generation == gate.generation;
}
