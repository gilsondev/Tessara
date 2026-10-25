#include "platform/face_prefs.h"

#include "domain/dashboard_layout.h"
#include "platform/widget_settings.h"

namespace {

clock_face::Options g_opts{};
bool g_loaded = false;

}  // namespace

void face_prefs::reload() {
    clock_face::Options opts;
    char buf[24] = {};
    if (widget_settings::get(dashboard::id_clock(), "time_format", buf, sizeof(buf))) {
        opts.time = clock_face::parse_time_format(buf);
    }
    buf[0] = '\0';
    if (widget_settings::get(dashboard::id_date(), "date_format", buf, sizeof(buf))) {
        opts.date = clock_face::parse_date_format(buf);
    }
    g_opts = opts;
    g_loaded = true;
}

clock_face::Options face_prefs::options() {
    if (!g_loaded) reload();
    return g_opts;
}
