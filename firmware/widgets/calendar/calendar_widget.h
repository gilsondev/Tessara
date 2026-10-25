#pragma once

#include "calendar/snapshot.h"
#include "tessara/widget.h"

namespace tessara {

const char *calendar_widget_id();
TsWidget *create_calendar_widget();

void calendar_widget_paint(TsWidget *widget, const calendar::View &view);
void calendar_widget_apply_theme(TsWidget *widget);

}  // namespace tessara
