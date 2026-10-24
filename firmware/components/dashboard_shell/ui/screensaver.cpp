#include "ui/screensaver.h"

#include "platform/daytime.h"
#include "ui/kit.h"
#include "ui/theme.h"

#include <cstdio>
#include <cstring>

namespace {

// Cartões HH:MM — dois pontos no eixo, não o glifo ":" (baseline desalinha).
constexpr int kDigitW = 56;
constexpr int kDigitH = 80;
constexpr int kHalfH = kDigitH / 2;
constexpr int kGap = 8;
constexpr int kColonW = 18;
constexpr int kDotSize = 7;
constexpr int kDotOffY = 13;  // acima/abaixo da costura
constexpr int kClockW = 4 * kDigitW + kColonW + 4 * kGap;

// Flip realista (refs: CSS3D Lab / Solari / Tock):
// fase 1 = folha de cima cai (gravidade, ease-in);
// fase 2 = folha de baixo aterrissa (ease-out).
constexpr uint32_t kFallMs = 280;
constexpr uint32_t kRiseMs = 300;

lv_obj_t *screen = nullptr;
lv_obj_t *clock_box = nullptr;
lv_obj_t *digits[4] = {};
lv_obj_t *top_labels[4] = {};
lv_obj_t *bottom_labels[4] = {};
lv_obj_t *flap_top[4] = {};
lv_obj_t *flap_bot[4] = {};
lv_obj_t *colon_box = nullptr;
char shown[5] = "----";
int hour_cache = -1;
int minute_cache = -1;
bool shown_flag = false;
bool flipping[4] = {};
char pending_new[4] = {};
char pending_old[4] = {};

bool any_flipping() {
    for (int i = 0; i < 4; ++i) {
        if (flipping[i]) return true;
    }
    return false;
}

void style_half(lv_obj_t *half, bool top) {
    lv_obj_set_size(half, kDigitW, kHalfH);
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

// Centro do glifo no eixo do cartão: metade de cima mostra a parte superior, etc.
void place_top_glyph(lv_obj_t *label) { lv_obj_align(label, LV_ALIGN_CENTER, 0, kHalfH / 2); }
void place_bottom_glyph(lv_obj_t *label) { lv_obj_align(label, LV_ALIGN_CENTER, 0, -kHalfH / 2); }

void set_digit_text(int index, char ch) {
    char one[2] = {ch, '\0'};
    if (top_labels[index]) lv_label_set_text(top_labels[index], one);
    if (bottom_labels[index]) lv_label_set_text(bottom_labels[index], one);
}

void style_flap(lv_obj_t *flap, bool top) {
    lv_obj_set_size(flap, kDigitW, kHalfH);
    lv_obj_set_style_bg_color(flap, lv_color_hex(top ? 0xE4E0D6 : 0xDCD7CC), 0);
    lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(flap, 8, 0);
    lv_obj_set_style_clip_corner(flap, true, 0);
    lv_obj_set_style_border_width(flap, 0, 0);
    lv_obj_set_style_pad_all(flap, 0, 0);
    lv_obj_add_flag(flap, LV_OBJ_FLAG_HIDDEN);
}

void reset_flap_top(lv_obj_t *flap) {
    lv_obj_set_height(flap, kHalfH);
    lv_obj_align(flap, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(flap, LV_OPA_COVER, 0);
}

void reset_flap_bot(lv_obj_t *flap) {
    lv_obj_set_height(flap, 3);
    lv_obj_align(flap, LV_ALIGN_TOP_MID, 0, 0);  // hinge = costura
    lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(flap, LV_OPA_COVER, 0);
}

lv_obj_t *make_dot(lv_obj_t *parent, int off_y) {
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, kDotSize, kDotSize);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xF2F0E8), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, off_y);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    return dot;
}

lv_obj_t *make_colon(lv_obj_t *parent) {
    // Caixa da altura do cartão: pontos simétricos em torno da costura (eixo do flip).
    lv_obj_t *box = kit::bare(parent);
    lv_obj_set_size(box, kColonW, kDigitH);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    make_dot(box, -kDotOffY);
    make_dot(box, kDotOffY);
    return box;
}

lv_obj_t *make_digit(lv_obj_t *parent, int index) {
    lv_obj_t *card = kit::bare(parent);
    lv_obj_set_size(card, kDigitW, kDigitH);
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

    // Folha que cai (hinge na costura = bottom do half superior).
    lv_obj_t *ft = kit::bare(top);
    style_flap(ft, true);
    reset_flap_top(ft);
    flap_top[index] = ft;

    // Folha que sobe (hinge na costura = top do half inferior).
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
    for (int i = 0; i < 4; ++i) {
        if (flap_top[i] == flap) return i;
    }
    return -1;
}

int flap_bot_index(lv_obj_t *flap) {
    for (int i = 0; i < 4; ++i) {
        if (flap_bot[i] == flap) return i;
    }
    return -1;
}

void shade_by_height(lv_obj_t *flap, int32_t h) {
    // Escurece perto do eixo (folha de lado) — leitura comum nos demos CSS.
    if (h < 16) {
        lv_opa_t opa = static_cast<lv_opa_t>((h * LV_OPA_COVER) / 16);
        if (opa < 70) opa = 70;
        lv_obj_set_style_bg_opa(flap, opa, 0);
    } else {
        lv_obj_set_style_bg_opa(flap, LV_OPA_COVER, 0);
    }
}

void finish_digit(int i) {
    if (i < 0 || i > 3) return;
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
    if (i < 0 || i > 3 || !flap_bot[i]) {
        finish_digit(i);
        return;
    }
    lv_obj_t *fb = flap_bot[i];
    // Novo dígito sob a folha que sobe; bottom antigo some ao abrir.
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
    lv_anim_set_values(&anim, 3, kHalfH);
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
    // Topo novo já estava sob a folha; agora aterra a metade de baixo.
    if (i >= 0) {
        char one[2] = {pending_new[i], '\0'};
        if (top_labels[i]) lv_label_set_text(top_labels[i], one);
        start_rise(i);
    }
}

void flip_digit(int index, char next) {
    if (index < 0 || index > 3 || !flap_top[index] || !top_labels[index]) return;
    char cur = shown[index];
    if (cur == next) return;
    if (flipping[index]) return;

    lv_obj_t *ft = flap_top[index];
    lv_anim_delete(ft, nullptr);
    if (flap_bot[index]) lv_anim_delete(flap_bot[index], nullptr);

    char old_txt[2] = {cur, '\0'};
    char new_txt[2] = {next, '\0'};
    // Anatomia clássica: top estático = novo (atrás da queda); bottom = antigo até a aterrissagem.
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
    lv_anim_set_values(&anim, kHalfH, 3);
    lv_anim_set_duration(&anim, kFallMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&anim, fall_ready);
    lv_anim_start(&anim);
}

void set_time_digits(int hour, int minute, bool animate) {
    char buf[5];
    snprintf(buf, sizeof(buf), "%02d%02d", hour, minute);
    for (int i = 0; i < 4; ++i) {
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
}

void center_clock() {
    if (!clock_box) return;
    lv_anim_delete(clock_box, nullptr);
    lv_obj_center(clock_box);
}

}  // namespace

void screensaver::begin() {
    if (screen) return;
    screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    clock_box = kit::bare(screen);
    lv_obj_set_size(clock_box, kClockW, kDigitH);
    lv_obj_set_flex_flow(clock_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clock_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(clock_box, kGap, 0);
    lv_obj_add_flag(clock_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    for (int i = 0; i < 4; ++i) {
        if (i == 2) colon_box = make_colon(clock_box);
        digits[i] = make_digit(clock_box, i);
        flipping[i] = false;
        pending_new[i] = 0;
        pending_old[i] = 0;
    }
    center_clock();
}

void screensaver::show() {
    begin();
    if (!screen) return;
    int hour = 0, minute = 0, wday = 0, day = 0, month = 0;
    if (!daytime::local(hour, minute, wday, day, month)) {
        hour = 0;
        minute = 0;
    }
    for (int i = 0; i < 4; ++i) {
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
    memset(shown, '-', 4);
    shown[4] = '\0';
    set_time_digits(hour, minute, false);
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
    int hour = 0, minute = 0, wday = 0, day = 0, month = 0;
    if (daytime::local(hour, minute, wday, day, month)) {
        if (hour != hour_cache || minute != minute_cache) set_time_digits(hour, minute, true);
    }
}

void screensaver::debug_flip() {
    if (!shown_flag) show();
    int hour = hour_cache >= 0 ? hour_cache : 0;
    int minute = minute_cache >= 0 ? minute_cache : 0;
    minute += 1;
    if (minute >= 60) {
        minute = 0;
        hour = (hour + 1) % 24;
    }
    set_time_digits(hour, minute, true);
}

void screensaver::apply_theme() {
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
}
