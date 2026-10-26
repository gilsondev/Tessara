#pragma once

#include "mac/focus_snapshot.h"
#include "tessera/widget.h"

namespace tessera {

const char *mac_widget_id();
TsWidget *create_mac_widget();

void mac_widget_paint(TsWidget *widget, const focus::View &view, bool agent_ok);
void mac_widget_apply_theme(TsWidget *widget);

}  // namespace tessera
