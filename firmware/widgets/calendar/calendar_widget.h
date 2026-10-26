#pragma once

#include "calendar/snapshot.h"
#include "tessera/widget.h"

namespace tessera {

const char *calendar_widget_id();
TsWidget *create_calendar_widget();

void calendar_widget_paint(TsWidget *widget, const calendar::View &view);
void calendar_widget_apply_theme(TsWidget *widget);

}  // namespace tessera
