#pragma once

#include "domain/model.h"

#include <stdint.h>

// What the panel knows about each list: views, cached tasks and how fresh they are. Single-threaded (UI loop).
namespace task_store {

enum class State : uint8_t { Empty, Ready, Failed };

bool begin();
void set_views(const ViewItem *views, int count);
int view_count();
const ViewItem &view(int index);

State state(int view);
const TaskItem *tasks(int view, int *count);
// Bumped on every change of a list's contents; the UI uses it to know its bitmaps are stale.
uint32_t version(int view);
uint32_t age_ms(int view);
const char *error(int view);

void store(int view, const TaskItem *tasks, int count);
// A fetch that finished after the list was hidden. Shown again later without a new request.
void remember(const char *query, const TaskItem *tasks, int count);
void fail(int view, const char *error);
// Optimistic completion: drops the row locally. Returns its index or -1.
int remove(int view, const char *id);
void invalidate_all();

}  // namespace task_store
