#pragma once

#include <lvgl.h>
#include <stdint.h>

// One list rendered as a column of task rows, built a few rows per frame so a list opens instantly.
namespace task_page {

struct Page {
    lv_obj_t *obj = nullptr;
    int view = -1;
    uint32_t version = 0;  // task_store version the rows were built from
    int count = 0;
    int built = 0;
    // True while this page is the spinner because some list has not arrived yet.
    bool hold_for_rest = false;
};

// True while any visible list is still Empty (tasks not in yet).
bool lists_pending();

void create(Page &page, lv_obj_t *parent, int32_t width);
void destroy(Page &page);
// Hides the page now and frees it a few rows at a time from reap(); deleting a long list at once stalls a frame.
void retire(Page &page);
// Frees some retired rows; returns true while work remains.
bool reap();
// Fills from task_store: first rows, inline loader, error or empty message. Returns true if rows were added.
bool paint(Page &page, int view, lv_event_cb_t on_mark);
// Appends up to `rows` more rows; returns how many were added.
int grow(Page &page, int rows, lv_event_cb_t on_mark);
bool growing(const Page &page);
// Row index of a check mark, or -1.
int row_of(const Page &page, lv_obj_t *mark);
// Mirror a removal already applied to task_store.
void remove_row(Page &page, int row);
// Fills the mark, slides the row out and collapses it, then calls `done`.
void animate_completion(lv_obj_t *mark, lv_anim_completed_cb_t done, void *user_data);
// Width the rows are laid out for, given the screen width.
int32_t width_for(int32_t screen_width);

}  // namespace task_page
