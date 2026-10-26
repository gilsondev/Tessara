#pragma once

#include "media/snapshot.h"
#include "tessera/widget.h"

namespace tessera {

const char *media_widget_id();
TsWidget *create_media_widget();

void media_widget_paint(TsWidget *widget, const media::View &view, bool agent_ok);
void media_widget_apply_theme(TsWidget *widget);

}  // namespace tessera
