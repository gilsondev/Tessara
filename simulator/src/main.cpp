#include "host/sim_hooks.h"
#include "platform/settings.h"
#include "scenario.h"
#include "sim_app.h"
#include "sim_fonts.h"

#include <lvgl.h>
#include <drivers/sdl/lv_sdl_mouse.h>
#include <drivers/sdl/lv_sdl_window.h>

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace {

struct Args {
    int width = 320;
    int height = 480;
    std::string scenario = "normal";
    std::string fixtures_dir;
    std::string screenshot;
    std::string settings_dir = "/tmp/tessara-sim-settings";
    std::string screen = "home";
    bool headless = false;
    int frames = 8;
};

void usage(const char *argv0) {
    std::fprintf(stderr,
                 "usage: %s [--viewport WxH] [--scenario name] [--fixtures dir] "
                 "[--screen home|todoist|weather|aitokens|clock|settings] [--screenshot path.bmp] "
                 "[--settings dir] [--headless]\n",
                 argv0);
}

bool parse_args(int argc, char **argv, Args &a) {
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--viewport") && i + 1 < argc) {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) != 2 || w < 160 || h < 160) return false;
            a.width = w;
            a.height = h;
        } else if (!std::strcmp(argv[i], "--scenario") && i + 1 < argc) {
            a.scenario = argv[++i];
        } else if (!std::strcmp(argv[i], "--fixtures") && i + 1 < argc) {
            a.fixtures_dir = argv[++i];
        } else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            a.screenshot = argv[++i];
            a.headless = true;
        } else if (!std::strcmp(argv[i], "--settings") && i + 1 < argc) {
            a.settings_dir = argv[++i];
        } else if (!std::strcmp(argv[i], "--screen") && i + 1 < argc) {
            a.screen = argv[++i];
        } else if (!std::strcmp(argv[i], "--headless")) {
            a.headless = true;
        } else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            usage(argv[0]);
            std::exit(0);
        } else {
            return false;
        }
    }
    return true;
}

std::string fixture_path(const Args &a) {
    std::string dir = a.fixtures_dir;
    if (dir.empty()) dir = "fixtures";
    return dir + "/" + a.scenario + ".json";
}

void apply_scenario(const Scenario &s) {
    sim_time::set(s.hour, s.minute, s.wday, s.day, s.month, s.time_ok);
    sim_power::set_battery(s.battery_percent);
}

bool save_bmp(const char *path, int w, int h) {
    lv_display_t *disp = lv_display_get_default();
    if (!disp) return false;
    lv_refr_now(disp);
    auto *ren = static_cast<SDL_Renderer *>(lv_sdl_window_get_renderer(disp));
    if (!ren) {
        std::fprintf(stderr, "sim: no SDL renderer for screenshot\n");
        return false;
    }
    SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!tmp) return false;
    if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, tmp->pixels, tmp->pitch) != 0) {
        std::fprintf(stderr, "sim: RenderReadPixels: %s\n", SDL_GetError());
        SDL_FreeSurface(tmp);
        return false;
    }
    int rc = SDL_SaveBMP(tmp, path);
    SDL_FreeSurface(tmp);
    return rc == 0;
}

void print_report(const Scenario &s, const sim_app::State &app) {
    std::printf(
        "scenario=%s screen=%s tiles=%d todoist=%d weather=%d aitokens=%d clock_tile=%d wifi=%d bat=%d\n",
        s.name.empty() ? "?" : s.name.c_str(), sim_app::screen_name(app.screen), app.tile_count, app.has_todoist,
        app.has_weather, app.has_aitokens, app.has_clock_tile, s.wifi_online ? 1 : 0, s.battery_percent);
}

}  // namespace

int main(int argc, char **argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        usage(argv[0]);
        return 2;
    }

    if (args.headless) {
        setenv("SDL_VIDEODRIVER", "dummy", 0);
    }

    sim_settings::set_dir(args.settings_dir.c_str());

    std::string path = fixture_path(args);
    Scenario scenario;
    if (!load_scenario(path, scenario)) {
        std::fprintf(stderr, "sim: failed to load %s\n", path.c_str());
        return 1;
    }
    if (scenario.name.empty()) scenario.name = args.scenario;
    apply_scenario(scenario);
    settings::save_dashboard(scenario.dashboard);

    lv_init();
    sim_fonts::init();
    lv_display_t *disp = lv_sdl_window_create(args.width, args.height);
    if (!disp) {
        std::fprintf(stderr, "sim: lv_sdl_window_create failed\n");
        return 1;
    }
    lv_sdl_window_set_title(disp, ("Tessara sim — " + scenario.name).c_str());
    lv_indev_t *mouse = lv_sdl_mouse_create();
    (void)mouse;

    sim_app::State app;
    sim_app::init(app, scenario);

    const bool expect_todoist = dashboard::is_enabled(scenario.dashboard, dashboard::id_todoist());
    const bool expect_weather = dashboard::is_enabled(scenario.dashboard, dashboard::id_weather());
    const bool expect_ai = dashboard::is_enabled(scenario.dashboard, dashboard::id_aitokens());
    const bool expect_clock = dashboard::is_enabled(scenario.dashboard, dashboard::id_clock());
    if (app.has_todoist != expect_todoist || app.has_weather != expect_weather || app.has_aitokens != expect_ai ||
        app.has_clock_tile != expect_clock) {
        std::fprintf(stderr, "sim: compose mismatch vs dashboard fixture\n");
        return 3;
    }

    sim_app::Screen want = sim_app::parse_screen(args.screen.c_str());
    if (want != sim_app::Screen::Home) sim_app::show(app, want);
    print_report(scenario, app);

    for (int i = 0; i < args.frames; ++i) {
        lv_timer_handler();
        SDL_Delay(16);
    }

    if (!args.screenshot.empty()) {
        if (!save_bmp(args.screenshot.c_str(), args.width, args.height)) {
            std::fprintf(stderr, "sim: screenshot failed (%s)\n", args.screenshot.c_str());
            return 4;
        }
        std::printf("screenshot=%s\n", args.screenshot.c_str());
        sim_app::destroy(app);
        return 0;
    }

    if (args.headless) {
        sim_app::destroy(app);
        return 0;
    }

    while (!app.quit_requested) {
        lv_timer_handler();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) app.quit_requested = true;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) sim_app::on_escape(app);
        }
        SDL_Delay(16);
    }
    sim_app::destroy(app);
    return 0;
}
