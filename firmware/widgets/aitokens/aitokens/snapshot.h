#pragma once

#include <stddef.h>
#include <stdint.h>

// Parsed Bridge snapshot for the AI Tokens card and detail screen.
namespace aitokens {

// Bridge empurra ~5 s; só marca desatualizado após vários minutos sem push.
constexpr uint32_t kStaleMs = 180000;
constexpr int kMaxSources = 4;
constexpr int kDisplayMax = 28;  // Cursor: "3% · 13%" (+ folga)
constexpr int kResetMax = 16;
constexpr int kIdMax = 12;

enum class Kind : uint8_t { None, Tokens, Pct };

struct Source {
    char id[kIdMax] = {};
    bool available = false;
    Kind kind = Kind::None;
    int32_t value = 0;
    char display[kDisplayMax] = {};
    char reset[kResetMax] = {};
};

struct View {
    bool bridge_ok = false;
    bool ready = false;   // received at least one valid line this boot
    bool stale = false;   // ready but older than kStaleMs
    uint32_t received_ms = 0;
    Source sources[kMaxSources] = {};
    int source_count = 0;
};

// Mutates `view` from a full line starting with "AITOKENS". Returns false if ignored.
bool apply_line(View &view, const char *line, uint32_t now_ms);

// Marks stale when ready and age >= kStaleMs. Returns true if stale flag changed.
bool refresh_age(View &view, uint32_t now_ms);

const Source *find(const View &view, const char *id);

// Home tile thresholds. `Source::value` is remaining percent for every provider.
constexpr int kRemainLow = 50;
constexpr int kRemainAlert = 20;

enum class RowLevel : uint8_t { Ok, Low, Alert };

// Writes the home line, e.g. "Claude · 92% restante". Returns the remaining percent.
int home_row_text(const char *name, const Source &src, bool stale, char *out, size_t out_n, RowLevel *level);

}  // namespace aitokens
