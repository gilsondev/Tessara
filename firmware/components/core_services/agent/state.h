#pragma once

#include <stdint.h>

// Estado do Tessara Agent no painel (handshake USB AGENT). Snapshot só em RAM.
namespace agent {

constexpr int kDetailMax = 48;

constexpr uint32_t kFreshMs = 15000;  // sem AGENT recente → não está comunicando

struct View {
    bool ready = false;  // recebeu ≥1 AGENT válido neste boot
    bool ok = false;
    char detail[kDetailMax] = {};
    uint32_t received_ms = 0;
};

// Linha completa "AGENT\tv1\t…". Retorna false se ignorada.
bool apply_line(View &view, const char *line, uint32_t now_ms);

// Cópia para UI/API: ok só se o último AGENT ainda for fresco.
View for_display(const View &view, uint32_t now_ms);

}  // namespace agent
