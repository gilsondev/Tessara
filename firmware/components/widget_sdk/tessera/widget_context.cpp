#include "tessera/widget.h"

#include <cstdio>
#include <cstring>

bool tessera::WidgetContext::make_key(const char *key, char *out, size_t out_len) const {
    if (!key || !key[0] || !out || out_len < 4) return false;
    const char *id = instance_id_ && instance_id_[0] ? instance_id_ : "widget";
    int n = std::snprintf(out, out_len, "%s/%s", id, key);
    return n > 0 && static_cast<size_t>(n) < out_len;
}

bool tessera::WidgetContext::settings_get(const char *key, char *out, size_t out_len) const {
    if (!services_ || !services_->settings) return false;
    char full[128];
    if (!make_key(key, full, sizeof(full))) return false;
    return services_->settings->get(full, out, out_len);
}

bool tessera::WidgetContext::settings_set(const char *key, const char *value) const {
    if (!services_ || !services_->settings) return false;
    char full[128];
    if (!make_key(key, full, sizeof(full))) return false;
    return services_->settings->set(full, value);
}
