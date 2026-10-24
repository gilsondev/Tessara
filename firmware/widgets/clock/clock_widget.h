#pragma once

#include "tessara/services.h"
#include "tessara/widget.h"

namespace tessara {

const char *clock_widget_id();
TsWidget *create_clock_widget();

void clock_widget_paint(TsWidget *widget, const TimeSnapshot &snap);
void clock_widget_apply_theme(TsWidget *widget);

}  // namespace tessara
