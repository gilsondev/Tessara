#pragma once

#include "aitokens/snapshot.h"
#include "tessara/widget.h"

namespace tessara {

const char *aitokens_widget_id();
TsWidget *create_aitokens_widget();

void aitokens_widget_paint(TsWidget *widget, const aitokens::View &view);
void aitokens_widget_apply_theme(TsWidget *widget);

}  // namespace tessara
