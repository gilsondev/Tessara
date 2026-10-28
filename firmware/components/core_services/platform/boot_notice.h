#pragma once

#include <cstdio>

// One BOOT line, sent after the host is already reading. The first take() wins.
struct BootNotice {
    const char *why;
    bool sent;

    BootNotice() : why("other"), sent(false) {}

    void set(const char *next) {
        if (next && next[0]) why = next;
    }

    // Writes "BOOT\t<why>\n" once. Later calls return false and leave `out` alone.
    bool take(char *out, size_t cap) {
        if (sent || !out || cap < 8) return false;
        sent = true;
        std::snprintf(out, cap, "BOOT\t%s\n", why ? why : "other");
        return true;
    }
};

// "PANIC\t<pc>\t<exc_cause>\t<bt0,bt1,...>\n". Addresses are lowercase hex.
inline bool format_panic_line(char *out, size_t cap, uint32_t pc, uint32_t cause, const uint32_t *bt, int depth) {
    if (!out || cap < 16) return false;
    int wrote = std::snprintf(out, cap, "PANIC\t%lx\t%lx\t", static_cast<unsigned long>(pc),
                              static_cast<unsigned long>(cause));
    if (wrote < 0 || static_cast<size_t>(wrote) >= cap) return false;
    size_t at = static_cast<size_t>(wrote);
    if (depth < 0) depth = 0;
    if (depth > 16) depth = 16;
    for (int i = 0; i < depth; ++i) {
        uint32_t addr = bt ? bt[i] : 0;
        int step = std::snprintf(out + at, cap - at, i ? ",%lx" : "%lx", static_cast<unsigned long>(addr));
        if (step < 0 || static_cast<size_t>(step) >= cap - at) return false;
        at += static_cast<size_t>(step);
    }
    if (at + 2 > cap) return false;
    out[at++] = '\n';
    out[at] = '\0';
    return true;
}
