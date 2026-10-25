#include "ui/screensaver.h"

#include "clock_summary.h"
#include "platform/daytime.h"
#include "platform/face_prefs.h"
#include "ui/kit.h"
#include "ui/theme.h"

#include <cstdio>
#include <cstring>

namespace {

// Flip clock: HH:MM (4) or HH:MM:SS (6). Seconds mode uses smaller cards so it
// still fits the 320 px portrait width.
constexpr int kMaxDigits = 6;

// Flip realista (refs: CSS3D Lab / Solari / Tock):
// fase 1 = folha de cima cai (gravidade, ease-in);
// fase 2 = folha de baixo aterrissa (ease-out).
constexpr uint32_t kFallMs = 280;
constexpr uint32_t kRiseMs = 300;

struct Metrics {
    int digit_w;
    int digit_h;
    int half_h;
    int gap;
    int colon_w;
    int dot_size;
    int dot_off_y;
    int digit_n;
    int clock_w;
};

Metrics metrics_for(bool with_seconds) {
    Metrics m{};
    if (with_seconds) {
        m.digit_w = 40;
        m.digit_h = 58;
        m.gap = 5;
        m.colon_w = 12;
        m.dot_size = 5;
        m.dot_off_y = 9;
        m.digit_n = 6;
    } else {
        m.digit_w = 56;
        m.digit_h = 80;
        m.gap = 8;
        m.colon_w = 18;
        m.dot_size = 7;
        m.dot_off_y = 13;
        m.digit_n = 4;
    }
    m.half_h = m.digit_h / 2;
    // Children: digits + (digit_n/2 - 0) colons → digit_n digits + (digit_n/2 - 1)? 
    // Layout: HH : MM [: SS] → digit_n + digit_n/2 - 1? For 4: 4 dig + 1 colon = 5 kids, 4 gaps.
    // For 6: 6 dig + 2 colon = 8 kids, 7 gaps. Flex pad_column applies between kids.
    int kids = m.digit_n + (m.digit_n / 2) - 1;  // 4→5, 6→8? 6/2-1=2 colons, 6+2=8 ✓; 4/2-1=1, 4+1=5 ✓
    m.clock_w = m.digit_n * m.digit_w + (kids - m.digit_n) * m.colon_w + (kids - 1) * m.gap;
    return m;
}

lv_obj_t *screen = nullptr;
lv_obj_t *clock_box = nullptr;
lv_obj_t *digits[kMaxDigits] = {};
lv_obj_t *top_labels[kMaxDigits] = {};
lv_obj_t *bottom_labels[kMaxDigits] = {};
lv_obj_t *flap_top[kMaxDigits] = {};
lv_obj_t *flap_bot[kMaxDigits] = {};
lv_obj_t *colon_boxes[2] = {};
char shown[kMaxDigits + 1] = "------";
int hour_cache = -1;
int minute_cache = -1;
int second_cache = -1;
bool shown_flag = false;
bool flipping[kMaxDigits] = {};
char pending_new[kMaxDigits] = {};
char pending_old[kMaxDigits] = {};
Metrics metrics = metrics_for(false);
bool built_with_seconds = false;

bool want_seconds() {
    return face_prefs::options().time == clock_face::TimeFormat::Hms;
}

bool any_flipping() {
    for (int i = 0; i < metrics.digit_n; ++i) {
        if (flipping[i]) return true;
    }
    return false;
}

void style_half(lv_obj_t *half, bool top) {
    lv_obj_set_size(half, metrics.digit_w, metrics.half_h);
    lv_obj_set_style_bg_color(half, lv_color_hex(top ? 0xF5F3EC : 0xEDEAE2), 0);
    lv_obj_set_style_bg_opa(half, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(half, 0, 0);
    lv_obj_set_style_pad_all(half, 0, 0);
    lv_obj_set_style_radius(half, 8, 0);
    lv_obj_set_style_clip_corner(half, true, 0);
    lv_obj_remove_flag(half, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(half, LV_OBJ_FLAG_CLICKABLE);
}

void style_digit_label(lv_obj_t *label) {
    lv_obj_set_style_text_font(label, &theme::display_font(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x1A1A18), 0);
}

void place_top_glyph(lv_obj_t *label) { lv_obj_align(label, LV_ALIGN_CENTER, 0, metrics.half_h / 2); }
void place_bottom_glyph(lv_obj_t *label) { lv_obj_align(label, LV_ALIGN_CENTER, 0, -metrics.half_h / 2); }

void set_digit_text(int index, char ch) {
    char one[2] = {ch, '\0'};
    if (top_labels[index]) lv_label_set_text(top_labels[index], one);
    if (bottom_labels[index]) lv_label_set_text(bottom_labels[index], one);
}

void style_flap(lv_obj_t *flap, bool top) {
    lv_obj_set_size(flap, metrics.digit_w, metrics.half_h);
    lv_obj_set_style_bg_color(flap, lv_color_hex(top ? 0xE4E0D6 : 0xDCD7CC), 0);
    lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(flap, 8, 0);
    lv_obj_set_style_clip_corner(flap, true, 0);
    lv_obj_set_style_border_width(flap, 0, 0);
    lv_obj_set_style_pad_all(flap, 0, 0);
    lv_obj_add_flag(flap, LV_OBJ_FLAG_HIDDEN);
}

void reset_flap_top(lv_obj_t *flap) {
    lv_obj_set_height(flap, metrics.half_h);
    lv_obj_align(flap, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(flap, LV_OPA_COVER, 0);
}

void reset_flap_bot(lv_obj_t *flap) {
    lv_obj_set_height(flap, 3);
    lv_obj_align(flap, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(flap, LV_OPA_COVER, 0);
}

lv_obj_t *make_dot(lv_obj_t *parent, int off_y) {
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, metrics.dot_size, metrics.dot_size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xF2F0E8), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, off_y);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    return dot;
}

lv_obj_t *make_colon(lv_obj_t *parent) {
    lv_obj_t *box = kit::bare(parent);
    lv_obj_set_size(box, metrics.colon_w, metrics.digit_h);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    make_dot(box, -metrics.dot_off_y);
    make_dot(box, metrics.dot_off_y);
    return box;
}

lv_obj_t *make_digit(lv_obj_t *parent, int index) {
    lv_obj_t *card = kit::bare(parent);
    lv_obj_set_size(card, metrics.digit_w, metrics.digit_h);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF2F0E8), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bottom = kit::bare(card);
    style_half(bottom, false);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *bottom_lab = lv_label_create(bottom);
    style_digit_label(bottom_lab);
    lv_label_set_text(bottom_lab, "0");
    place_bottom_glyph(bottom_lab);
    bottom_labels[index] = bottom_lab;

    lv_obj_t *top = kit::bare(card);
    style_half(top, true);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *top_lab = lv_label_create(top);
    style_digit_label(top_lab);
    lv_label_set_text(top_lab, "0");
    place_top_glyph(top_lab);
    top_labels[index] = top_lab;

    lv_obj_t *ft = kit::bare(top);
    style_flap(ft, true);
    reset_flap_top(ft);
    flap_top[index] = ft;

    lv_obj_t *fb = kit::bare(bottom);
    style_flap(fb, false);
    reset_flap_bot(fb);
    flap_bot[index] = fb;

    lv_obj_t *seam = lv_obj_create(card);
    lv_obj_remove_style_all(seam);
    lv_obj_set_size(seam, lv_pct(100), 2);
    lv_obj_align(seam, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(seam, lv_color_hex(0x2A2A28), 0);
    lv_obj_set_style_bg_opa(seam, LV_OPA_50, 0);
    lv_obj_remove_flag(seam, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(seam);

    return card;
}

int flap_top_index(lv_obj_t *flap) {
    for (int i = 0; i < metrics.digit_n; ++i) {
        if (flap_top[i] == flap) return i;
    }
    return -1;
}

int flap_bot_index(lv_obj_t *flap) {
    for (int i = 0; i < metrics.digit_n; ++i) {
        if (flap_bot[i] == flap) return i;
    }
    return -1;
}

void shade_by_height(lv_obj_t *flap, int32_t h) {
    if (h < 16) {
        lv_opa_t opa = static_cast<lv_opa_t>((h * LV_OPA_COVER) / 16);
        if (opa < 70) opa = 70;
        lv_obj_set_style_bg_opa(flap, opa, 0);
    } else {
        lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
    }
}

void finish_digit(int i) {
    if (i < 0 || i >= metrics.digit_n) return;
    char ch = pending_new[i] ? pending_new[i] : shown[i];
    char one[2] = {ch, '\0'};
    if (top_labels[i]) lv_label_set_text(top_labels[i], one);
    if (bottom_labels[i]) lv_label_set_text(bottom_labels[i], one);
    if (flap_top[i]) {
        reset_flap_top(flap_top[i]);
        lv_obj_add_flag(flap_top[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (flap_bot[i]) {
        reset_flap_bot(flap_bot[i]);
        lv_obj_add_flag(flap_bot[i], LV_OBJ_FLAG_HIDDEN);
    }
    flipping[i] = false;
    pending_new[i] = 0;
    pending_old[i] = 0;
}

void rise_exec(void *obj, int32_t h);
void rise_ready(lv_anim_t *anim);
void fall_exec(void *obj, int32_t h);
void fall_ready(lv_anim_t *anim);

void start_rise(int i) {
    if (i < 0 || i >= metrics.digit_n || !flap_bot[i]) {
        finish_digit(i);
        return;
    }
    lv_obj_t *fb = flap_bot[i];
    char one[2] = {pending_new[i], '\0'};
    if (bottom_labels[i]) lv_label_set_text(bottom_labels[i], one);

    lv_anim_delete(fb, nullptr);
    reset_flap_bot(fb);
    lv_obj_remove_flag(fb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(fb);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, fb);
    lv_anim_set_exec_cb(&anim, rise_exec);
    lv_anim_set_values(&anim, 3, metrics.half_h);
    lv_anim_set_duration(&anim, kRiseMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&anim, rise_ready);
    lv_anim_start(&anim);
}

void rise_exec(void *obj, int32_t h) {
    auto *flap = static_cast<lv_obj_t *>(obj);
    if (h < 2) h = 2;
    lv_obj_set_height(flap, h);
    lv_obj_align(flap, LV_ALIGN_TOP_MID, 0, 0);
    shade_by_height(flap, h);
}

void rise_ready(lv_anim_t *anim) {
    lv_obj_t *flap = static_cast<lv_obj_t *>(anim->var);
    finish_digit(flap_bot_index(flap));
}

void fall_exec(void *obj, int32_t h) {
    auto *flap = static_cast<lv_obj_t *>(obj);
    if (h < 2) h = 2;
    lv_obj_set_height(flap, h);
    lv_obj_align(flap, LV_ALIGN_TOP_MID, 0, 0);
    shade_by_height(flap, h);
}

void fall_ready(lv_anim_t *anim) {
    lv_obj_t *flap = static_cast<lv_obj_t *>(anim->var);
    int i = flap_top_index(flap);
    if (flap) {
        reset_flap_top(flap);
        lv_obj_add_flag(flap, LV_OBJ_FLAG_HIDDEN);
    }
    if (i >= 0) {
        char one[2] = {pending_new[i], '\0'};
        if (top_labels[i]) lv_label_set_text(top_labels[i], one);
        start_rise(i);
    }
}

void flip_digit(int index, char next) {
    if (index < 0 || index >= metrics.digit_n || !flap_top[index] || !top_labels[index]) return;
    char cur = shown[index];
    if (cur == next) return;
    if (flipping[index]) return;

    lv_obj_t *ft = flap_top[index];
    lv_anim_delete(ft, nullptr);
    if (flap_bot[index]) lv_anim_delete(flap_bot[index], nullptr);

    char old_txt[2] = {cur, '\0'};
    char new_txt[2] = {next, '\0'};
    lv_label_set_text(top_labels[index], new_txt);
    lv_label_set_text(bottom_labels[index], old_txt);
    pending_old[index] = cur;
    pending_new[index] = next;
    shown[index] = next;
    flipping[index] = true;

    reset_flap_top(ft);
    lv_obj_remove_flag(ft, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ft);
    if (flap_bot[index]) lv_obj_add_flag(flap_bot[index], LV_OBJ_FLAG_HIDDEN);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ft);
    lv_anim_set_exec_cb(&anim, fall_exec);
    lv_anim_set_values(&anim, metrics.half_h, 3);
    lv_anim_set_duration(&anim, kFallMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&anim, fall_ready);
    lv_anim_start(&anim);
}

void set_time_digits(int hour, int minute, int second, bool animate) {
    char buf[kMaxDigits + 1];
    if (metrics.digit_n >= 6) {
        snprintf(buf, sizeof(buf), "%02d%02d%02d", hour, minute, second);
    } else {
        snprintf(buf, sizeof(buf), "%02d%02d", hour, minute);
    }
    for (int i = 0; i < metrics.digit_n; ++i) {
        if (animate && shown[i] != '-' && shown[i] != buf[i]) {
            flip_digit(i, buf[i]);
        } else if (!flipping[i]) {
            set_digit_text(i, buf[i]);
            if (flap_top[i]) {
                lv_anim_delete(flap_top[i], nullptr);
                reset_flap_top(flap_top[i]);
                lv_obj_add_flag(flap_top[i], LV_OBJ_FLAG_HIDDEN);
            }
            if (flap_bot[i]) {
                lv_anim_delete(flap_bot[i], nullptr);
                reset_flap_bot(flap_bot[i]);
                lv_obj_add_flag(flap_bot[i], LV_OBJ_FLAG_HIDDEN);
            }
            shown[i] = buf[i];
        }
    }
    hour_cache = hour;
    minute_cache = minute;
    second_cache = second;
}

void center_clock() {
    if (!clock_box) return;
    lv_anim_delete(clock_box, nullptr);
    lv_obj_center(clock_box);
}

void clear_digit_slots() {
    for (int i = 0; i < kMaxDigits; ++i) {
        digits[i] = nullptr;
        top_labels[i] = nullptr;
        bottom_labels[i] = nullptr;
        flap_top[i] = nullptr;
        flap_bot[i] = nullptr;
        flipping[i] = false;
        pending_new[i] = 0;
        pending_old[i] = 0;
        shown[i] = '-';
    }
    shown[kMaxDigits] = '\0';
    colon_boxes[0] = colon_boxes[1] = nullptr;
}

void build_clock_box() {
    if (!screen) return;
    if (clock_box) {
        for (int i = 0; i < kMaxDigits; ++i) {
            if (flap_top[i]) lv_anim_delete(flap_top[i], nullptr);
            if (flap_bot[i]) lv_anim_delete(flap_bot[i], nullptr);
        }
        lv_obj_delete(clock_box);
        clock_box = nullptr;
    }
    clear_digit_slots();
    metrics = metrics_for(want_seconds());
    built_with_seconds = metrics.digit_n >= 6;

    clock_box = kit::bare(screen);
    lv_obj_set_size(clock_box, metrics.clock_w, metrics.digit_h);
    lv_obj_set_flex_flow(clock_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clock_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(clock_box, metrics.gap, 0);
    lv_obj_add_flag(clock_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // HH : MM [: SS]
    digits[0] = make_digit(clock_box, 0);
    digits[1] = make_digit(clock_box, 1);
    colon_boxes[0] = make_colon(clock_box);
    digits[2] = make_digit(clock_box, 2);
    digits[3] = make_digit(clock_box, 3);
    if (metrics.digit_n >= 6) {
        colon_boxes[1] = make_colon(clock_box);
        digits[4] = make_digit(clock_box, 4);
        digits[5] = make_digit(clock_box, 5);
    }
    center_clock();
}

void ensure_layout() {
    const bool secs = want_seconds();
    if (!clock_box || built_with_seconds != secs) build_clock_box();
}

void read_now(int &hour, int &minute, int &second) {
    hour = minute = second = 0;
    int wday = 0, day = 0, month = 0, year = 0;
    if (!daytime::local(hour, minute, second, wday, day, month, year)) {
        hour = minute = second = 0;
    }
}

}  // namespace

void screensaver::begin() {
    if (screen) return;
    screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    build_clock_box();
}

void screensaver::show() {
    begin();
    if (!screen) return;
    ensure_layout();
    int hour = 0, minute = 0, second = 0;
    read_now(hour, minute, second);
    for (int i = 0; i < metrics.digit_n; ++i) {
        flipping[i] = false;
        if (flap_top[i]) {
            lv_anim_delete(flap_top[i], nullptr);
            reset_flap_top(flap_top[i]);
            lv_obj_add_flag(flap_top[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (flap_bot[i]) {
            lv_anim_delete(flap_bot[i], nullptr);
            reset_flap_bot(flap_bot[i]);
            lv_obj_add_flag(flap_bot[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    memset(shown, '-', metrics.digit_n);
    shown[metrics.digit_n] = '\0';
    set_time_digits(hour, minute, second, false);
    center_clock();
    lv_screen_load(screen);
    shown_flag = true;
}

void screensaver::hide() { shown_flag = false; }

bool screensaver::visible() { return shown_flag; }

void screensaver::tick(uint32_t now_ms) {
    (void)now_ms;
    if (!shown_flag || !screen) return;
    if (any_flipping()) return;
    const bool before_secs = built_with_seconds;
    ensure_layout();
    int hour = 0, minute = 0, second = 0;
    read_now(hour, minute, second);
    const bool secs = metrics.digit_n >= 6;
    const bool rebuilt = before_secs != built_with_seconds;
    if (rebuilt || hour != hour_cache || minute != minute_cache || (secs && second != second_cache)) {
        set_time_digits(hour, minute, second, !rebuilt);
    }
}

void screensaver::debug_flip() {
    if (!shown_flag) show();
    ensure_layout();
    int hour = hour_cache >= 0 ? hour_cache : 0;
    int minute = minute_cache >= 0 ? minute_cache : 0;
    int second = second_cache >= 0 ? second_cache : 0;
    if (metrics.digit_n >= 6) {
        second += 1;
        if (second >= 60) {
            second = 0;
            minute += 1;
        }
    } else {
        minute += 1;
    }
    if (minute >= 60) {
        minute = 0;
        hour = (hour + 1) % 24;
    }
    set_time_digits(hour, minute, second, true);
}

void screensaver::apply_theme() {
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
}
