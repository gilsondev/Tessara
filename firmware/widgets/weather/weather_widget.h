#pragma once

#include "tessera/widget.h"
#include "weather/cache.h"

namespace tessera {

const char *weather_widget_id();
TsWidget *create_weather_widget();

// Push a fresh view into the mounted weather detail (generation-checked by caller via runtime).
void weather_widget_paint(TsWidget *widget, const weather::View &view);
void weather_widget_apply_theme(TsWidget *widget);

}  // namespace tessera
