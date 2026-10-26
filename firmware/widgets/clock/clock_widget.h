#pragma once

#include "tessera/services.h"
#include "tessera/widget.h"

namespace tessera {

const char *clock_widget_id();
TsWidget *create_clock_widget();

void clock_widget_paint(TsWidget *widget, const TimeSnapshot &snap);
void clock_widget_apply_theme(TsWidget *widget);

}  // namespace tessera
