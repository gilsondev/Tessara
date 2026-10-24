#include "domain/pending_closes.h"

#include "hal/board.h"
#include "services/text.h"

#include <cstring>

namespace {
constexpr char kNamespace[] = "todoist";
constexpr char kKey[] = "feitas";

hal::StoragePort *store() { return hal::board().storage; }
}  // namespace

int pending_closes::load(char ids[][20], int cap) {
    if (cap > kMax) cap = kMax;
    if (cap < 0) cap = 0;
    char raw[kMax * 21] = {};
    if (auto *s = store()) s->get(kNamespace, kKey, raw, sizeof(raw));
    int count = 0;
    const char *at = raw;
    while (*at && count < cap) {
        const char *end = std::strchr(at, '\n');
        size_t len = end ? static_cast<size_t>(end - at) : std::strlen(at);
        while (len && (at[len - 1] == ' ' || at[len - 1] == '\r')) --len;
        while (len && (*at == ' ' || *at == '\r')) {
            ++at;
            --len;
        }
        if (len > 0 && len < 20) {
            char tmp[20];
            std::memcpy(tmp, at, len);
            tmp[len] = '\0';
            text::copy(ids[count], 20, tmp);
            count++;
        }
        at = end ? end + 1 : at + len;
    }
    return count;
}

void pending_closes::save(const char ids[][20], int count) {
    char body[kMax * 21];
    size_t used = 0;
    body[0] = '\0';
    if (count > kMax) count = kMax;
    if (count < 0) count = 0;
    for (int i = 0; i < count; ++i) {
        if (!ids[i][0] || strchr(ids[i], '\n')) continue;
        size_t len = strlen(ids[i]);
        if (len == 0 || len >= 20) continue;
        if (used + len + 1 >= sizeof(body)) break;
        if (used) body[used++] = '\n';
        memcpy(body + used, ids[i], len);
        used += len;
        body[used] = '\0';
    }
    auto *s = store();
    if (!s) return;
    if (!body[0]) s->erase(kNamespace, kKey);
    else s->set(kNamespace, kKey, body);
}
