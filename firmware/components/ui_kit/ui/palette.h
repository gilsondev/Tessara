#pragma once

#include <stdint.h>

// Screen colors as 0xRRGGBB. Dark is the preferred reading. Red and priority do not follow the mode.
namespace palette {

enum class Mode : uint8_t { Light, Dark };

void set_mode(Mode mode);
Mode mode();

// Ts tokens. Widgets use these names. The short aliases below match them.
uint32_t background();
uint32_t surface();
uint32_t surface_elevated();
uint32_t text_primary();
uint32_t text_secondary();
uint32_t border();
// Verde de interação. Muda entre os temas.
uint32_t primary();
// Texto sobre o verde: branco no claro, quase preto no escuro.
uint32_t on_primary();
// Peça de destaque da marca. Muda entre os temas. Não pinta botões.
uint32_t accent();
uint32_t warning();
uint32_t error();
uint32_t sky();
// Todoist identity. Only for a small mark, not for the platform theme.
uint32_t red();
// Service marks. They stay put in both themes and never replace primary.
uint32_t widget_todoist();
uint32_t widget_calendar();
uint32_t widget_weather();
uint32_t widget_aitokens();
uint32_t priority(uint8_t level);

uint32_t paper();
uint32_t ink();
uint32_t muted();
uint32_t wash();
uint32_t hairline();
uint32_t sand();

}  // namespace palette
