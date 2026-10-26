#pragma once

#include "tessera/widget.h"

namespace tessera {

const char *todoist_widget_id();
TsWidget *create_todoist_widget();

// Shell binds detail attach: parent is the detail_host slot (may be null).
void todoist_widget_bind_show(void (*show_in)(void *ui_parent));

}  // namespace tessera
