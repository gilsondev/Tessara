#include "tessara/runtime.h"

#include "aitokens_widget.h"
#include "clock_widget.h"
#include "date_widget.h"
#include "greeting_widget.h"
#include "todoist_widget.h"
#include "weather_widget.h"

namespace tessara {

void register_builtin_widgets() {
    Runtime &rt = runtime();
    rt.register_type(greeting_widget_id(), create_greeting_widget);
    rt.register_type(date_widget_id(), create_date_widget);
    rt.register_type(todoist_widget_id(), create_todoist_widget);
    rt.register_type(weather_widget_id(), create_weather_widget);
    rt.register_type(aitokens_widget_id(), create_aitokens_widget);
    rt.register_type(clock_widget_id(), create_clock_widget);
}

}  // namespace tessara
