#pragma once

#include <stddef.h>
#include <stdint.h>

// The home card reads this. It does not decide when to talk to Todoist.
namespace todoist {

struct ListTally {
    bool shown;
    bool ready;
    int count;
};

struct Summary {
    int total;
    bool pending;
};

Summary summarize(const ListTally *rows, int count);

// Home card label: empty while any visible list is still loading (the tile shows a spinner).
// The number appears only when every visible list is ready.
void format_home_count(const Summary &summary, char *out, size_t cap);

// A network result is stamped when it is sent. Leaving Todoist bumps the gate, so a late result is dropped.
struct Gate {
    uint32_t generation = 1;
};

uint32_t stamp(const Gate &gate);
void bump(Gate &gate);
bool accept(const Gate &gate, uint32_t response_generation);

}  // namespace todoist
