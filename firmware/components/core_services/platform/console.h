#pragma once

#include <stdint.h>

// Line commands over USB serial. Parsing lives here; what a command does belongs to the handler.
class ConsoleHandler {
public:
    virtual ~ConsoleHandler() = default;
    virtual void on_wifi(const char *ssid, const char *pass) = 0;
    virtual void on_token(const char *token) = 0;
    virtual void on_reload() = 0;
    virtual void on_snapshot() = 0;
    virtual void on_perf() = 0;
    virtual void on_drag(int x0, int y0, int x1, int y1, uint32_t ms) = 0;
    virtual void on_flash() = 0;
    // Opens a short one-shot window for PUT /agentd/agentd.tgz (portal / pack-agentd).
    virtual void on_agent_push(bool on) = 0;
    // First-run portal, and the way back to the credentials copied by it.
    virtual void on_enter_setup() = 0;
    virtual void on_restore() = 0;
    // Drops only the token, after copying the current credentials. The home then explains how to paste it.
    virtual void on_drop_token() = 0;
    virtual void on_sd_status() = 0;
    // Agent / AI Tokens / Calendar USB pushes (full line).
    virtual void on_aitokens(const char *line) = 0;
    virtual void on_agent(const char *line) = 0;
    virtual void on_calendar(const char *line) = 0;
    virtual void on_todoist(const char *line) = 0;
    virtual void on_media(const char *line) = 0;
    virtual void on_focus(const char *line) = 0;
    // macOS appearance: THEME\tv1\t1\tdark|light (Agente).
    virtual void on_theme(const char *line) = 0;
    // Screen rest test: "", "wake", "saver", "fade", "off".
    virtual void on_rest(const char *arg) = 0;
    // Clean restart asked by the Mac (`REBOOT`). Does not fix a wedged USB stack.
    virtual void on_reboot() = 0;
};

namespace console {
void poll(ConsoleHandler &handler);
}
