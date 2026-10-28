#include "domain/list_prefs.h"

#include "hal/board.h"
#include "platform/flash_trace.h"
#include "services/text.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

namespace {
constexpr char kNamespace[] = "todoist";
constexpr char kKey[] = "listas";
constexpr size_t kRawCap = kMaxViews * 222;

struct Saved {
    bool shown;
    char query[220];
};

hal::StoragePort *store() { return hal::board().storage; }

// The USB snapshot applies these prefs while the other core is pushing a PSRAM
// frame. Reading NVS in that window disables the flash cache and panics the chip.
// The first call (boot, before the Mac is writing) fills this; later calls stay in RAM.
// The bytes live in PSRAM so the internal heap still has room for Todoist TLS.
char *cached = nullptr;
bool cached_ok = false;

char *cache_buf() {
    if (cached) return cached;
#if defined(ARDUINO)
    cached = static_cast<char *>(heap_caps_malloc(kRawCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
    if (!cached) cached = static_cast<char *>(std::malloc(kRawCap));
    if (cached) cached[0] = '\0';
    return cached;
}

void remember(const char *body) {
    char *dest = cache_buf();
    if (!dest) return;
    if (!body) body = "";
    size_t n = std::strlen(body);
    if (n >= kRawCap) n = kRawCap - 1;
    std::memcpy(dest, body, n);
    dest[n] = '\0';
    cached_ok = true;
}

void fill_cache() {
    if (cached_ok) return;
    flash_trace::note(__func__);
    char *dest = cache_buf();
    if (!dest) return;
    dest[0] = '\0';
    if (auto *s = store()) s->get(kNamespace, kKey, dest, kRawCap);
    cached_ok = true;
}

int load(Saved *saved, int cap) {
    fill_cache();
    int count = 0;
    const char *at = cache_buf();
    if (!at) return 0;
    while (*at && count < cap) {
        bool shown = *at != '-';
        if (*at == '+' || *at == '-') ++at;
        const char *end = std::strchr(at, '\n');
        size_t len = end ? static_cast<size_t>(end - at) : std::strlen(at);
        if (len >= sizeof(saved[count].query)) len = sizeof(saved[count].query) - 1;
        std::memcpy(saved[count].query, at, len);
        saved[count].query[len] = '\0';
        for (char *p = saved[count].query; *p; ++p)
            if (*p == '\n') *p = ' ';
        saved[count].shown = shown;
        if (saved[count].query[0]) ++count;
        at = end ? end + 1 : at + len;
    }
    return count;
}

void fill(list_prefs::Choice &choice, const ViewItem &item, bool shown) {
    text::copy(choice.name, sizeof(choice.name), item.name);
    text::copy(choice.query, sizeof(choice.query), item.query);
    choice.shown = shown;
}
}  // namespace

int list_prefs::arrange(const ViewItem *catalog, int count, Choice *out, int cap) {
    if (count > kMaxViews) count = kMaxViews;
    if (count > cap) count = cap;
    // PSRAM: this runs on the 16 KB loop stack, under the USB parser.
    static Saved *saved = nullptr;
    if (!saved) {
#if defined(ARDUINO)
        saved = static_cast<Saved *>(
            heap_caps_malloc(sizeof(Saved) * kMaxViews, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
        if (!saved) saved = static_cast<Saved *>(std::malloc(sizeof(Saved) * kMaxViews));
        if (saved) std::memset(saved, 0, sizeof(Saved) * kMaxViews);
    }
    if (!saved) {
        for (int i = 0; i < count; ++i) fill(out[i], catalog[i], true);
        return count;
    }
    int saved_n = load(saved, kMaxViews);
    if (!saved_n) {
        for (int i = 0; i < count; ++i) fill(out[i], catalog[i], true);
        return count;
    }
    bool used[kMaxViews] = {};
    int n = 0;
    for (int s = 0; s < saved_n && n < cap; ++s) {
        for (int i = 0; i < count; ++i) {
            if (used[i] || strcmp(catalog[i].query, saved[s].query)) continue;
            used[i] = true;
            fill(out[n], catalog[i], saved[s].shown);
            n++;
            break;
        }
    }
    for (int i = 0; i < count && n < cap; ++i) {
        if (used[i]) continue;
        fill(out[n], catalog[i], true);
        n++;
    }
    return n;
}

int list_prefs::visible(const ViewItem *catalog, int count, ViewItem *out, int cap) {
    static Choice *choices = nullptr;
    if (!choices) {
#if defined(ARDUINO)
        choices = static_cast<Choice *>(
            heap_caps_malloc(sizeof(Choice) * kMaxViews, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
        if (!choices) choices = static_cast<Choice *>(std::malloc(sizeof(Choice) * kMaxViews));
    }
    if (!choices) {
        int fallback = count < cap ? count : cap;
        for (int i = 0; i < fallback; ++i) out[i] = catalog[i];
        return fallback;
    }
    int n = arrange(catalog, count, choices, kMaxViews);
    int shown = 0;
    for (int i = 0; i < n && shown < cap; ++i) {
        if (!choices[i].shown) continue;
        text::copy(out[shown].name, sizeof(out[shown].name), choices[i].name);
        text::copy(out[shown].query, sizeof(out[shown].query), choices[i].query);
        shown++;
    }
    if (shown || !count) return shown;
    int fallback = count < cap ? count : cap;
    for (int i = 0; i < fallback; ++i) out[i] = catalog[i];
    return fallback;
}

bool list_prefs::save(const ViewItem *catalog, int count, const char names[][28], const bool *shown, int names_n) {
    flash_trace::note(__func__);
    if (count > kMaxViews) count = kMaxViews;
    if (names_n > kMaxViews) names_n = kMaxViews;
    int visible_n = 0;
    for (int i = 0; i < names_n; ++i)
        if (shown[i]) visible_n++;
    if (names_n && !visible_n) return false;

    auto *s = store();
    if (!s) return false;

    bool untouched = names_n == count;
    for (int i = 0; untouched && i < count; ++i) untouched = shown[i] && !strcmp(names[i], catalog[i].name);
    if (untouched) {
        s->erase(kNamespace, kKey);
        remember("");
        return true;
    }

    bool used[kMaxViews] = {};
    char body[kRawCap];
    size_t at = 0;
    int kept = 0;
    for (int i = 0; i < names_n; ++i) {
        int found = -1;
        for (int c = 0; c < count; ++c) {
            if (!used[c] && !strcmp(catalog[c].name, names[i])) found = c;
        }
        if (found < 0) continue;
        used[found] = true;
        if (shown[i]) kept++;
        int wrote = snprintf(body + at, sizeof(body) - at, "%c%s\n", shown[i] ? '+' : '-', catalog[found].query);
        if (wrote < 0 || static_cast<size_t>(wrote) >= sizeof(body) - at) break;
        at += static_cast<size_t>(wrote);
    }
    for (int c = 0; c < count; ++c) {
        if (used[c]) continue;
        int wrote = snprintf(body + at, sizeof(body) - at, "+%s\n", catalog[c].query);
        if (wrote < 0 || static_cast<size_t>(wrote) >= sizeof(body) - at) break;
        at += static_cast<size_t>(wrote);
        kept++;
    }
    if (!kept) return false;
    if (!s->set(kNamespace, kKey, body)) return false;
    remember(body);
    return true;
}
