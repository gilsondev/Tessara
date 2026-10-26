#pragma once

#include "agent/state.h"
#include "aitokens/snapshot.h"
#include "domain/device_settings.h"

// Full-screen "Ajustes" page over the task list.
namespace settings_panel {

using ChangeFn = void (*)(const DeviceSettings &settings);
using DismissFn = void (*)();
using RequestAiTokensFn = void (*)();
// Widget-scoped pref (clock/date face). widget_id is org.tessera.*.
using WidgetPrefFn = void (*)(const char *widget_id, const char *key, const char *value);

void open(ChangeFn on_change, DismissFn on_dismiss = nullptr, RequestAiTokensFn on_request_aitokens = nullptr,
          WidgetPrefFn on_widget_pref = nullptr);
// Latest values; repaints when the page is open.
void show_values(const DeviceSettings &settings, const DeviceInfo &info);
void show_face(const char *timezone, const char *time_format, const char *date_format);
void show_aitokens(const aitokens::View &view);
void show_agent(const agent::View &view, bool pack_on_sd = false, const char *pack_version = nullptr);
void close();
bool visible();
void relayout();

}  // namespace settings_panel
