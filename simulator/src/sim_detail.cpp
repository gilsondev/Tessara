#include "sim_detail.h"

#include "domain/model.h"
#include "domain/task_store.h"
#include "ui/aitokens_page.h"
#include "ui/clock_page.h"
#include "ui/todoist_panel.h"
#include "ui/weather_page.h"
#include "tessara/services.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

bool g_todoist_begun = false;
bool g_weather_begun = false;
bool g_aitokens_begun = false;
bool g_clock_begun = false;

void seed_list(int view, const std::vector<ScenarioTask> &tasks) {
    if (tasks.empty()) {
        TaskItem none{};
        task_store::store(view, &none, 0);
        return;
    }
    TaskItem items[kMaxTasks];
    int n = static_cast<int>(tasks.size());
    if (n > kMaxTasks) n = kMaxTasks;
    for (int i = 0; i < n; ++i) {
        items[i] = {};
        if (!tasks[i].id.empty()) {
            std::snprintf(items[i].id, sizeof(items[i].id), "%s", tasks[i].id.c_str());
        } else {
            std::snprintf(items[i].id, sizeof(items[i].id), "%d", 9000000 + view * 1000 + i + 1);
        }
        std::snprintf(items[i].content, sizeof(items[i].content), "%s", tasks[i].content.c_str());
        // Meta mirrors todoist_client::fill_meta — due/prazo phrases only, never priority labels.
        if (!tasks[i].meta.empty()) {
            std::snprintf(items[i].meta, sizeof(items[i].meta), "%s", tasks[i].meta.c_str());
        }
        int p = tasks[i].priority;
        if (p < 1) p = 1;
        if (p > 4) p = 4;
        items[i].priority = static_cast<uint8_t>(p);
    }
    task_store::store(view, items, n);
}

}  // namespace

weather::View sim_detail::weather_view(const Scenario &s) {
    weather::View v = {};
    v.placed = s.weather_placed;
    v.ready = s.weather_ready;
    v.stale = s.weather_stale;
    v.failed = s.weather_failed;
    std::snprintf(v.name, sizeof(v.name), "%s", s.weather_place.c_str());
    v.forecast.temperature_c = s.weather_temp_c;
    v.forecast.code = s.weather_code;

    // daytime::local uses tm_mon (0–11); Open-Meteo-style dates need 1–12.
    int month = s.month + 1;
    if (month < 1 || month > 12) month = 10;
    int year = 2026;
    auto add_days = [](int y, int m, int d, int add, int *oy, int *om, int *od) {
        static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        d += add;
        auto dim = [&](int yy, int mm) {
            int n = mdays[(mm - 1) % 12];
            if (mm == 2 && ((yy % 4 == 0 && yy % 100 != 0) || yy % 400 == 0)) n = 29;
            return n;
        };
        int n = dim(y, m);
        while (d > n) {
            d -= n;
            m += 1;
            if (m > 12) {
                m = 1;
                y += 1;
            }
            n = dim(y, m);
        }
        *oy = y;
        *om = m;
        *od = d;
    };
    for (int i = 0; i < 7; ++i) {
        int y = 0, m = 0, d = 0;
        add_days(year, month, s.day, i, &y, &m, &d);
        std::snprintf(v.forecast.days[i].date, sizeof(v.forecast.days[i].date), "%04d-%02d-%02d", y, m, d);
        v.forecast.days[i].temperature_max_c = s.weather_hi_c - i;
        v.forecast.days[i].temperature_min_c = s.weather_lo_c - i / 2;
        v.forecast.days[i].code = s.weather_code;
    }
    v.forecast.days[0].temperature_max_c = s.weather_hi_c;
    v.forecast.days[0].temperature_min_c = s.weather_lo_c;
    return v;
}

aitokens::View sim_detail::aitokens_view(const Scenario &s) {
    aitokens::View v = {};
    v.bridge_ok = s.aitokens_ready;
    v.ready = s.aitokens_ready;
    v.stale = s.aitokens_stale;
    if (!s.aitokens_ready) return v;

    auto put = [&](const char *id, aitokens::Kind kind, int32_t value, const char *display, const char *reset) {
        if (v.source_count >= aitokens::kMaxSources) return;
        aitokens::Source &src = v.sources[v.source_count++];
        std::snprintf(src.id, sizeof(src.id), "%s", id);
        src.available = true;
        src.kind = kind;
        src.value = value;
        std::snprintf(src.display, sizeof(src.display), "%s", display);
        if (reset && reset[0]) std::snprintf(src.reset, sizeof(src.reset), "%s", reset);
    };

    char buf[aitokens::kDisplayMax];
    // agent/codex.py: remaining 5h · week
    std::snprintf(buf, sizeof(buf), "%d%% · %d%%", s.aitokens_codex_5h, s.aitokens_codex_week);
    put("codex", aitokens::Kind::Pct, s.aitokens_codex_week, buf, s.aitokens_codex_reset.c_str());

    std::snprintf(buf, sizeof(buf), "%d%%", s.aitokens_claude_pct);
    put("claude", aitokens::Kind::Pct, s.aitokens_claude_pct, buf, s.aitokens_claude_reset.c_str());

    // agent/cursor.py: used models · other
    std::snprintf(buf, sizeof(buf), "%d%% · %d%%", s.aitokens_cursor_a, s.aitokens_cursor_b);
    put("cursor", aitokens::Kind::Pct, s.aitokens_cursor_a, buf, nullptr);
    return v;
}

void sim_detail::load_todoist_store(const Scenario &scenario) {
    static bool once = false;
    if (!once) {
        if (!task_store::begin()) {
            std::fprintf(stderr, "sim: task_store::begin failed\n");
            return;
        }
        once = true;
    }

    std::vector<ViewItem> views;
    if (!scenario.lists.empty()) {
        for (const auto &list : scenario.lists) {
            ViewItem v{};
            std::snprintf(v.name, sizeof(v.name), "%s", list.name.c_str());
            const char *query = list.query.empty() ? list.name.c_str() : list.query.c_str();
            std::snprintf(v.query, sizeof(v.query), "%s", query);
            views.push_back(v);
        }
    } else {
        ViewItem hoje{};
        std::snprintf(hoje.name, sizeof(hoje.name), "Hoje");
        std::snprintf(hoje.query, sizeof(hoje.query), "today | overdue");
        views.push_back(hoje);
    }

    task_store::set_views(views.data(), static_cast<int>(views.size()));

    if (!scenario.lists.empty()) {
        for (int i = 0; i < static_cast<int>(scenario.lists.size()); ++i) seed_list(i, scenario.lists[i].tasks);
    } else {
        seed_list(0, scenario.tasks);
    }
}

void sim_detail::open_todoist(lv_event_cb_t on_back) {
    if (!g_todoist_begun) {
        todoist_panel::Hooks hooks;
        hooks.on_back = on_back;
        hooks.idle_allowed = []() { return true; };
        todoist_panel::begin(hooks);
        g_todoist_begun = true;
    }
    todoist_panel::set_views(0);
    todoist_panel::show();
}

void sim_detail::open_weather(const Scenario &scenario, lv_event_cb_t on_back) {
    if (!g_weather_begun) {
        weather_page::begin(on_back);
        g_weather_begun = true;
    }
    // show() builds the tree; paint() needs list/labels already created.
    weather_page::show();
    weather_page::paint(weather_view(scenario));
}

void sim_detail::open_aitokens(const Scenario &scenario, lv_event_cb_t on_back) {
    if (!g_aitokens_begun) {
        aitokens_page::begin(on_back, nullptr);
        g_aitokens_begun = true;
    }
    aitokens_page::show();
    aitokens_page::paint(aitokens_view(scenario));
}

void sim_detail::open_clock(const Scenario &scenario, lv_event_cb_t on_back) {
    if (!g_clock_begun) {
        clock_page::begin(on_back);
        g_clock_begun = true;
    }
    tessara::TimeSnapshot snap;
    snap.hour = scenario.hour;
    snap.minute = scenario.minute;
    snap.wday = scenario.wday;
    snap.day = scenario.day;
    snap.month = scenario.month;
    snap.synced = scenario.time_ok;
    clock_page::show();
    clock_page::paint(snap);
}
