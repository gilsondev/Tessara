#pragma once

#include "mac/snapshot.h"
#include "tessara/widget.h"

namespace tessara {

const char *mac_widget_id();
TsWidget *create_mac_widget();

void mac_widget_paint(TsWidget *widget, const mac::View &view, bool agent_ok);
void mac_widget_apply_theme(TsWidget *widget);

}  // namespace tessara
