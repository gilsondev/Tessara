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
//
// Physical Solari / CSS3D Lab anatomy (four leaves per digit):
//   1. static top  = NEW glyph top (revealed as old top falls)
//   2. static bottom = OLD glyph bottom (until new bottom rises)
//   3. falling flap = OLD glyph top, hinges at seam, ease-in
//   4. rising flap  = NEW glyph bottom, hinges at seam, ease-out after fall
// Height animation approximates rotateX without a 3D transform.
constexpr int kMaxDigits = 6;
// Seconds must finish well under 1 s (fall+rise+stagger). Minutes can be slower.
constexpr uint32_t kFallMsHms = 90;
constexpr uint32_t kRiseMsHms = 95;
constexpr uint32_t kFallMsHm = 150;
constexpr uint32_t kRiseMsHm = 160;
constexpr uint32_t kStaggerMs = 28;

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
    int kids = m.digit_n + (m.digit_n / 2) - 1;
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
lv_obj_t *flap_top_lab[kMaxDigits] = {};
lv_obj_t *flap_bot_lab[kMaxDigits] = {};
lv_obj_t *colon_boxes[2] = {};
char shown[kMaxDigits + 1] = "------";
char queued[kMaxDigits] = {};
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

uint32_t fall_ms() { return metrics.digit_n >= 6 ? kFallMsHms : kFallMsHm; }
uint32_t rise_ms() { return metrics.digit_n >= 6 ? kRiseMsHms : kRiseMsHm; }

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

// Pin digit optical center on the seam so foreshortening clips correctly as height changes.
void pin_glyph_on_seam_above(lv_obj_t *label) {
    if (!label) return;
    lv_obj_update_layout(label);
    lv_coord_t h = lv_obj_get_height(label);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, h / 2);
}
void pin_glyph_on_seam_below(lv_obj_t *label) {
    if (!label) return;
    lv_obj_update_layout(label);
    lv_coord_t h = lv_obj_get_height(label);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, -h / 2);
}

void set_static_digit(int index, char ch) {
    char one[2] = {ch, '\0'};
    if (top_labels[index]) lv_label_set_text(top_labels[index], one);
    if (bottom_labels[index]) lv_label_set_text(bottom_labels[index], one);
}

void style_flap(lv_obj_t *flap, bool top) {
    lv_obj_set_size(flap, metrics.digit_w, metrics.half_h);
    lv_obj_set_style_bg_color(flap, lv_color_hex(top ? 0xE8E4DA : 0xE0DBD0), 0);
    lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(flap, 8, 0);
    lv_obj_set_style_clip_corner(flap, true, 0);
    lv_obj_set_style_border_width(flap, 0, 0);
    lv_obj_set_style_pad_all(flap, 0, 0);
    lv_obj_add_flag(flap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(flap, LV_OBJ_FLAG_SCROLLABLE);
}

void reset_flap_top(lv_obj_t *flap) {
    // Hinge at the seam (bottom of the top half) — free edge is the card top.
    lv_obj_set_height(flap, metrics.half_h);
    lv_obj_align(flap, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(flap, LV_OPA_COVER, 0);
}

void reset_flap_bot(lv_obj_t *flap) {
    // Hinge at the seam (top of the bottom half) — free edge grows downward.
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

    // Falling leaf: old top glyph rides on the flap.
    lv_obj_t *ft = kit::bare(top);
    style_flap(ft, true);
    reset_flap_top(ft);
    lv_obj_t *ftl = lv_label_create(ft);
    style_digit_label(ftl);
    lv_label_set_text(ftl, "0");
    place_top_glyph(ftl);
    flap_top[index] = ft;
    flap_top_lab[index] = ftl;

    // Rising leaf: new bottom glyph rides on the flap.
    lv_obj_t *fb = kit::bare(bottom);
    style_flap(fb, false);
    reset_flap_bot(fb);
    lv_obj_t *fbl = lv_label_create(fb);
    style_digit_label(fbl);
    lv_label_set_text(fbl, "0");
    place_bottom_glyph(fbl);
    flap_bot[index] = fb;
    flap_bot_lab[index] = fbl;

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
    // Edge-on ≈ darker leaf — never drop opacity (that ghosts the digit underneath).
    const int full = metrics.half_h > 0 ? metrics.half_h : 1;
    int t = (h * 100) / full;
    if (t < 0) t = 0;
    if (t > 100) t = 100;
    // Open ≈ #E8E4DA; edge-on ≈ #9A958C
    const int r = 0x9A + ((0xE8 - 0x9A) * t) / 100;
    const int g = 0x95 + ((0xE4 - 0x95) * t) / 100;
    const int b = 0x8C + ((0xDA - 0x8C) * t) / 100;
    lv_obj_set_style_bg_color(flap, lv_color_hex((r << 16) | (g << 8) | b), 0);
    lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
}

void flip_digit(int index, char next, uint32_t delay_ms);

void finish_digit(int i) {
    if (i < 0 || i >= metrics.digit_n) return;
    char ch = pending_new[i] ? pending_new[i] : shown[i];
    set_static_digit(i, ch);
    shown[i] = ch;
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
    char next = queued[i];
    queued[i] = 0;
    if (next && next != ch) flip_digit(i, next, 0);
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
    // Keep static bottom as OLD until the rising leaf covers it; glyph rides on flap.
    if (flap_bot_lab[i]) {
        lv_label_set_text(flap_bot_lab[i], one);
        pin_glyph_on_seam_below(flap_bot_lab[i]);
    }

    lv_anim_delete(fb, nullptr);
    reset_flap_bot(fb);
    lv_obj_remove_flag(fb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(fb);
    if (flap_bot_lab[i]) pin_glyph_on_seam_below(flap_bot_lab[i]);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, fb);
    lv_anim_set_exec_cb(&anim, rise_exec);
    lv_anim_set_values(&anim, 3, metrics.half_h);
    lv_anim_set_duration(&anim, rise_ms());
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
    int i = flap_bot_index(flap);
    if (i >= 0) pin_glyph_on_seam_below(flap_bot_lab[i]);
}

void rise_ready(lv_anim_t *anim) {
    lv_obj_t *flap = static_cast<lv_obj_t *>(anim->var);
    finish_digit(flap_bot_index(flap));
}

void fall_exec(void *obj, int32_t h) {
    auto *flap = static_cast<lv_obj_t *>(obj);
    if (h < 2) h = 2;
    lv_obj_set_height(flap, h);
    lv_obj_align(flap, LV_ALIGN_BOTTOM_MID, 0, 0);  // hinge stays on the seam
    shade_by_height(flap, h);
    int i = flap_top_index(flap);
    if (i >= 0) pin_glyph_on_seam_above(flap_top_lab[i]);
}

void fall_ready(lv_anim_t *anim) {
    lv_obj_t *flap = static_cast<lv_obj_t *>(anim->var);
    int i = flap_top_index(flap);
    if (flap) {
        reset_flap_top(flap);
        lv_obj_add_flag(flap, LV_OBJ_FLAG_HIDDEN);
    }
    if (i >= 0) {
        // New top already set at flip start; land the new bottom.
        start_rise(i);
    }
}

void flip_digit(int index, char next, uint32_t delay_ms) {
    if (index < 0 || index >= metrics.digit_n || !flap_top[index] || !top_labels[index]) return;
    char cur = shown[index];
    if (cur == next && !flipping[index]) return;
    if (flipping[index]) {
        // Keep latest target — physical cards also skip intermediate if spun fast.
        queued[index] = next;
        return;
    }
    if (cur == next) return;

    lv_obj_t *ft = flap_top[index];
    lv_anim_delete(ft, nullptr);
    if (flap_bot[index]) lv_anim_delete(flap_bot[index], nullptr);

    char old_txt[2] = {cur, '\0'};
    char new_txt[2] = {next, '\0'};
    // Anatomy: static top = NEW (behind fall); static bottom = OLD until rise lands.
    if (top_labels[index]) lv_label_set_text(top_labels[index], new_txt);
    if (bottom_labels[index]) lv_label_set_text(bottom_labels[index], old_txt);
    if (flap_top_lab[index]) {
        lv_label_set_text(flap_top_lab[index], old_txt);
        pin_glyph_on_seam_above(flap_top_lab[index]);
    }
    if (flap_bot_lab[index]) {
        lv_label_set_text(flap_bot_lab[index], new_txt);
        pin_glyph_on_seam_below(flap_bot_lab[index]);
    }
    pending_old[index] = cur;
    pending_new[index] = next;
    shown[index] = next;
    flipping[index] = true;
    queued[index] = 0;

    reset_flap_top(ft);
    lv_obj_remove_flag(ft, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ft);
    if (flap_top_lab[index]) pin_glyph_on_seam_above(flap_top_lab[index]);
    if (flap_bot[index]) lv_obj_add_flag(flap_bot[index], LV_OBJ_FLAG_HIDDEN);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ft);
    lv_anim_set_exec_cb(&anim, fall_exec);
    lv_anim_set_values(&anim, metrics.half_h, 3);
    lv_anim_set_duration(&anim, fall_ms());
    lv_anim_set_delay(&anim, delay_ms);
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
    int stagger_i = 0;
    for (int i = 0; i < metrics.digit_n; ++i) {
        if (animate && shown[i] != '-' && shown[i] != buf[i]) {
            // Left→right cascade like a Solari board; units still lead visually
            // because they change alone most of the time.
            flip_digit(i, buf[i], static_cast<uint32_t>(stagger_i) * kStaggerMs);
            ++stagger_i;
        } else if (!flipping[i]) {
            set_static_digit(i, buf[i]);
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
            queued[i] = 0;
        } else if (shown[i] != buf[i]) {
            queued[i] = buf[i];
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
        flap_top_lab[i] = nullptr;
        flap_bot_lab[i] = nullptr;
        flipping[i] = false;
        pending_new[i] = 0;
        pending_old[i] = 0;
        queued[i] = 0;
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
        queued[i] = 0;
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
    const bool before_secs = built_with_seconds;
    ensure_layout();
    int hour = 0, minute = 0, second = 0;
    read_now(hour, minute, second);
    const bool secs = metrics.digit_n >= 6;
    const bool rebuilt = before_secs != built_with_seconds;
    // Never drop a second/minute — queue onto digits that are mid-flip.
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
