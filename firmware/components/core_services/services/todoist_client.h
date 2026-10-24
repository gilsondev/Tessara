#pragma once

#include "domain/model.h"

#include <stddef.h>

// Blocking Todoist REST client. Call only from the network worker, never from UI code.
namespace todoist {

void set_token(const char *token);
// Hoje first, then favourite filters in the user's order; falls back to the GTD contexts when sync fails.
int load_views(ViewItem *views, int cap);
bool load_tasks(const char *query, TaskItem *tasks, int *count, char *error, size_t error_len);
// status_out receives the HTTP status, or 0 when the link never answered.
// A missing or already closed task counts as success so the panel does not retry it.
bool close_task(const char *id, char *error, size_t error_len, int *status_out = nullptr);

}  // namespace todoist
