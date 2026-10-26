#include "ui/aitokens_page.h"

#include "platform/settings.h"
#include "ui/aitokens_logos.h"
#include "ui/kit.h"
#include "ui/palette.h"
#include "ui/theme.h"

#include <cstdio>
#include <cstring>

namespace {
constexpr int kWarnPct = 50;
constexpr int kCriticalPct = 20;
constexpr uint32_t kBarAnimMs = 750;

lv_obj_t *screen = nullptr;
lv_obj_t *body = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *status_label = nullptr;

struct Block {
    lv_obj_t *box = nullptr;
    lv_obj_t *alert = nullptr;
    lv_obj_t *value = nullptr;
    lv_obj_t *bar = nullptr;
    lv_obj_t *bar2 = nullptr;       // Cursor: Other Models
    lv_obj_t *stripes = nullptr;   // listras diagonais animadas (estilo Coyier)
    lv_obj_t *stripes2 = nullptr;
    lv_obj_t *row1_label = nullptr; // "Modelos"
    lv_obj_t *row2_label = nullptr; // "Outros"
    lv_obj_t *sub = nullptr;
    lv_color_t brand = lv_color_hex(0x10A37F);  // cor da logo (barra saudável)
    bool dual = false;
    bool dual_used = false;  // true=Cursor (% usados); false=Codex (% restante 5h/semana)
    int last_fill = -1;
    int last_fill2 = -1;
};

// Cores alinhadas às marcas oficiais / logos no cartão.
lv_color_t brand_codex() { return lv_color_hex(0x10A37F); }   // OpenAI green
lv_color_t brand_claude() { return lv_color_hex(0xD97757); }  // Anthropic terracotta
lv_color_t brand_cursor() { return lv_color_hex(0xE8E8E8); }  // Cursor (claro no fundo escuro)

Block codex_block;
Block claude_block;
Block cursor_block;
bool awaiting_refresh = false;
aitokens::View cached_view = {};

void set_if(lv_obj_t *label, const char *text) {
    if (label && text && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

// Status may show/hide; kit::subsys_bar keeps a fixed two-line title slot so the
// bar height (and hit targets) stay stable.
void set_status(const char *text) {
    if (!status_label) return;
    if (!text || !text[0]) {
        lv_label_set_text(status_label, "");
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    set_if(status_label, text);
    lv_obj_set_style_text_opa(status_label, LV_OPA_COVER, 0);
    lv_obj_remove_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}

void reset_bar_anim() {
    codex_block.last_fill = -1;
    codex_block.last_fill2 = -1;
    claude_block.last_fill = -1;
    cursor_block.last_fill = -1;
    cursor_block.last_fill2 = -1;
}

lv_color_t bar_tone_remaining(lv_color_t brand, int remaining) {
    if (remaining < kCriticalPct) return theme::error();
    if (remaining < kWarnPct) return theme::warning();
    return brand;
}

lv_color_t bar_tone_used(lv_color_t brand, int used) {
    // Pouco usado = cor da marca; muito usado = alerta.
    if (used >= 100 - kCriticalPct) return theme::error();
    if (used >= 100 - kWarnPct) return theme::warning();
    return brand;
}

void style_bar(lv_obj_t *bar, lv_color_t tone) {
    if (!bar) return;
    lv_obj_set_style_bg_color(bar, theme::border(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, tone, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
}

// Chris Coyier-style diagonal stripes: contrast against the bar fill, scrolling right.
constexpr int kStripePeriod = 20;
constexpr int kStripeBand = 10;

struct StripeCtx {
    int32_t phase = 0;
    lv_color_t tone = lv_color_hex(0x10A37F);
};

lv_color_t stripe_contrast(lv_color_t tone) {
    lv_color32_t c = lv_color_to_32(tone, LV_OPA_COVER);
    const uint16_t y = static_cast<uint16_t>((c.red * 30u + c.green * 59u + c.blue * 11u) / 100u);
    // Barras claras → listra mais escura; barras saturadas → listra mais clara.
    if (y > 160) return lv_color_mix(tone, lv_color_black(), LV_OPA_40);
    return lv_color_mix(tone, lv_color_white(), LV_OPA_40);
}

void on_stripes_draw(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(event));
    auto *ctx = static_cast<StripeCtx *>(lv_obj_get_user_data(obj));
    if (!ctx) return;
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const int32_t w = lv_area_get_width(&coords);
    const int32_t h = lv_area_get_height(&coords);
    if (w <= 1 || h <= 0) return;

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = stripe_contrast(ctx->tone);
    line.opa = LV_OPA_50;
    line.width = kStripeBand;
    line.round_start = 0;
    line.round_end = 0;

    int32_t phase = ctx->phase % kStripePeriod;
    if (phase < 0) phase += kStripePeriod;
    // Estende além do retângulo visível para o clip comer as pontas da linha grossa.
    const int32_t over = kStripeBand + 6;
    const int32_t y_lo = coords.y2 + over;
    const int32_t y_hi = coords.y1 - over;
    const int32_t span = y_lo - y_hi;  // h + 2*over (−45°)
    for (int32_t x = -span - kStripePeriod + phase; x < w + span + kStripePeriod; x += kStripePeriod) {
        line.p1.x = coords.x1 + x;
        line.p1.y = y_lo;
        line.p2.x = coords.x1 + x + span;
        line.p2.y = y_hi;
        lv_draw_line(layer, &line);
    }
}

void on_stripes_delete(lv_event_t *event) {
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(event));
    auto *ctx = static_cast<StripeCtx *>(lv_obj_get_user_data(obj));
    if (!ctx) return;
    lv_free(ctx);
    lv_obj_set_user_data(obj, nullptr);
}

void stripe_phase(void *obj, int32_t v) {
    lv_obj_t *stripes = static_cast<lv_obj_t *>(obj);
    auto *ctx = static_cast<StripeCtx *>(lv_obj_get_user_data(stripes));
    if (!ctx) return;
    ctx->phase = v;
    lv_obj_t *wrap = lv_obj_get_parent(stripes);
    lv_obj_t *bar = wrap ? lv_obj_get_child(wrap, 0) : nullptr;
    if (bar) {
        const int32_t full = lv_obj_get_width(bar);
        const int val = lv_bar_get_value(bar);
        int32_t want = full > 0 ? (full * val) / 100 : 0;
        if (want < 0) want = 0;
        if (lv_obj_get_width(stripes) != want) lv_obj_set_width(stripes, want);
    }
    lv_obj_invalidate(stripes);
}

void set_stripe_tone(lv_obj_t *stripes, lv_color_t tone) {
    if (!stripes) return;
    auto *ctx = static_cast<StripeCtx *>(lv_obj_get_user_data(stripes));
    if (!ctx) return;
    ctx->tone = tone;
    lv_obj_invalidate(stripes);
}

void start_stripes(lv_obj_t *stripes, bool on) {
    if (!stripes) return;
    lv_anim_delete(stripes, stripe_phase);
    if (!on) {
        lv_obj_add_flag(stripes, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(stripes, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, stripes);
    lv_anim_set_exec_cb(&anim, stripe_phase);
    lv_anim_set_values(&anim, 0, kStripePeriod);
    lv_anim_set_duration(&anim, 900);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_start(&anim);
}

void pulse_exec(void *obj, int32_t v) {
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), LV_PART_INDICATOR);
}

void set_pulse(lv_obj_t *bar, bool critical) {
    if (!bar) return;
    lv_anim_delete(bar, pulse_exec);
    if (!critical) {
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
        return;
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, bar);
    lv_anim_set_exec_cb(&anim, pulse_exec);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_50);
    lv_anim_set_duration(&anim, 700);
    lv_anim_set_playback_duration(&anim, 700);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
}

void style_block_alert(Block &block, int remaining, bool ready) {
    if (!block.box) return;
    const bool warn = ready && remaining > 0 && remaining < kWarnPct;
    const bool critical = ready && remaining >= 0 && remaining < kCriticalPct;
    lv_color_t border = theme::border();
    if (critical) border = theme::error();
    else if (warn) border = theme::warning();
    lv_obj_set_style_border_color(block.box, border, 0);
    lv_obj_set_style_border_width(block.box, (warn || critical) ? 2 : 1, 0);
    if (block.value) {
        lv_obj_set_style_text_color(block.value, critical ? theme::error() : warn ? theme::warning() : theme::text_primary(),
                                    0);
    }
    if (block.alert) {
        if (warn || critical) {
            lv_label_set_text(block.alert, LV_SYMBOL_WARNING);
            lv_obj_set_style_text_color(block.alert, critical ? theme::error() : theme::warning(), 0);
            lv_obj_remove_flag(block.alert, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(block.alert, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void anim_bar(lv_obj_t *bar, lv_obj_t *stripes, int *last, int fill, bool animate, lv_color_t tone,
              bool pulse_critical) {
    if (!bar || !last) return;
    style_bar(bar, tone);
    set_stripe_tone(stripes, tone);
    lv_obj_set_style_anim_duration(bar, kBarAnimMs, 0);
    if (animate && *last != fill) {
        if (*last < 0) lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_bar_set_value(bar, fill, LV_ANIM_ON);
    } else {
        lv_bar_set_value(bar, fill, LV_ANIM_OFF);
    }
    *last = fill;
    set_pulse(bar, pulse_critical);
    start_stripes(stripes, fill > 0);
}

void set_bar_remaining(Block &block, int remaining, bool animate) {
    anim_bar(block.bar, block.stripes, &block.last_fill, remaining, animate,
             bar_tone_remaining(block.brand, remaining), remaining > 0 && remaining < kCriticalPct);
}

void set_bar_used(Block &block, lv_obj_t *bar, lv_obj_t *stripes, int *last, int used, bool animate) {
    anim_bar(bar, stripes, last, used, animate, bar_tone_used(block.brand, used), used >= 100 - kCriticalPct);
}

void make_bar(lv_obj_t *parent, lv_obj_t **bar_out, lv_obj_t **stripes_out, lv_color_t brand) {
    lv_obj_t *wrap = kit::bare(parent);
    lv_obj_set_width(wrap, lv_pct(100));
    lv_obj_set_height(wrap, 12);
    lv_obj_set_style_radius(wrap, 6, 0);
    lv_obj_set_style_clip_corner(wrap, true, 0);

    lv_obj_t *bar = lv_bar_create(wrap);
    lv_obj_set_size(bar, lv_pct(100), 12);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar, kBarAnimMs, 0);
    style_bar(bar, brand);

    lv_obj_t *stripes = lv_obj_create(wrap);
    lv_obj_remove_style_all(stripes);
    lv_obj_set_size(stripes, 0, 12);
    lv_obj_align(stripes, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(stripes, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(stripes, 6, 0);
    lv_obj_set_style_clip_corner(stripes, true, 0);
    lv_obj_remove_flag(stripes, LV_OBJ_FLAG_CLICKABLE);
    // Sem OVERFLOW_VISIBLE: o wrap com clip_corner corta pontas que passam da barra.
    lv_obj_add_flag(stripes, LV_OBJ_FLAG_HIDDEN);
    auto *ctx = static_cast<StripeCtx *>(lv_malloc(sizeof(StripeCtx)));
    if (ctx) {
        *ctx = StripeCtx{};
        ctx->tone = brand;
        lv_obj_set_user_data(stripes, ctx);
        lv_obj_add_event_cb(stripes, on_stripes_draw, LV_EVENT_DRAW_MAIN, nullptr);
        lv_obj_add_event_cb(stripes, on_stripes_delete, LV_EVENT_DELETE, nullptr);
    }

    if (bar_out) *bar_out = bar;
    if (stripes_out) *stripes_out = stripes;
}

lv_obj_t *make_pool_label(lv_obj_t *parent, const char *text) {
    lv_obj_t *row = kit::bare(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &theme::small_font(), 0);
    lv_obj_set_style_text_color(label, theme::text_secondary(), 0);
    lv_obj_set_flex_grow(label, 1);
    return label;
}

Block make_block(lv_obj_t *parent, const char *title, const lv_image_dsc_t *logo, bool dual, bool dual_used,
                 lv_color_t brand) {
    Block block;
    block.dual = dual;
    block.dual_used = dual_used;
    block.brand = brand;
    block.box = kit::bare(parent);
    lv_obj_set_width(block.box, lv_pct(100));
    lv_obj_set_height(block.box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(block.box, theme::surface(), 0);
    lv_obj_set_style_bg_opa(block.box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(block.box, 16, 0);
    lv_obj_set_style_border_width(block.box, 1, 0);
    lv_obj_set_style_border_color(block.box, theme::border(), 0);
    lv_obj_set_style_pad_all(block.box, 14, 0);
    lv_obj_set_flex_flow(block.box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(block.box, 6, 0);

    lv_obj_t *head = kit::bare(block.box);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 8, 0);

    if (logo) {
        lv_obj_t *mark = lv_image_create(head);
        lv_image_set_src(mark, logo);
        lv_obj_set_size(mark, 24, 24);
    }

    lv_obj_t *name = lv_label_create(head);
    lv_label_set_text(name, title);
    lv_obj_set_style_text_font(name, &theme::label_font(), 0);
    lv_obj_set_style_text_color(name, theme::text_secondary(), 0);
    lv_obj_set_flex_grow(name, 1);

    block.alert = lv_label_create(head);
    lv_label_set_text(block.alert, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(block.alert, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(block.alert, LV_OBJ_FLAG_HIDDEN);

    block.value = lv_label_create(block.box);
    lv_label_set_text(block.value, "—");
    lv_obj_set_style_text_font(block.value, &theme::title_font(), 0);
    lv_obj_set_style_text_color(block.value, theme::text_primary(), 0);

    if (dual) {
        if (dual_used) {
            block.row1_label = make_pool_label(block.box, "Modelos · 0% usados");
            make_bar(block.box, &block.bar, &block.stripes, brand);
            block.row2_label = make_pool_label(block.box, "Outros · 0% usados");
            make_bar(block.box, &block.bar2, &block.stripes2, brand);
            // Segunda barra um pouco mais suave (Other Models).
            style_bar(block.bar2, lv_color_mix(brand, theme::text_secondary(), LV_OPA_60));
        } else {
            block.row1_label = make_pool_label(block.box, "5 horas · — restante");
            make_bar(block.box, &block.bar, &block.stripes, brand);
            block.row2_label = make_pool_label(block.box, "Semana · — restante");
            make_bar(block.box, &block.bar2, &block.stripes2, brand);
        }
    } else {
        make_bar(block.box, &block.bar, &block.stripes, brand);
    }

    block.sub = lv_label_create(block.box);
    lv_label_set_text(block.sub, "");
    lv_obj_set_style_text_font(block.sub, &theme::small_font(), 0);
    lv_obj_set_style_text_color(block.sub, theme::text_secondary(), 0);
    return block;
}

bool parse_dual_pct(const char *display, int *a_out, int *b_out) {
    // "3% · 13%" ou "100% · 88%".
    if (!display || !a_out || !b_out) return false;
    int a = -1, b = -1;
    if (sscanf(display, "%d%% · %d%%", &a, &b) != 2 && sscanf(display, "%d%% ·%d%%", &a, &b) != 2 &&
        sscanf(display, "%d%*[% ·]%d%%", &a, &b) != 2) {
        return false;
    }
    if (a < 0 || a > 100 || b < 0 || b > 100) return false;
    *a_out = a;
    *b_out = b;
    return true;
}

bool source_ok(const aitokens::Source *src, bool ready) {
    return ready && src && src->available && src->display[0];
}

void set_block_visible(Block &block, bool show) {
    if (!block.box) return;
    if (show) lv_obj_remove_flag(block.box, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(block.box, LV_OBJ_FLAG_HIDDEN);
}

void paint_offline(Block &block, const aitokens::Source *src, bool ready) {
    set_block_visible(block, true);
    const char *msg = !ready ? "…" : (src && src->display[0] && strcmp(src->display, "—") != 0) ? src->display
                                                                                                  : "offline";
    set_if(block.value, msg);
    set_if(block.sub, "sem cotas agora");
    style_block_alert(block, 100, false);
    if (block.dual) {
        if (block.row1_label) set_if(block.row1_label, "—");
        if (block.row2_label) set_if(block.row2_label, "—");
        if (block.dual_used) {
            set_bar_used(block, block.bar, block.stripes, &block.last_fill, 0, false);
            if (block.bar2) {
                anim_bar(block.bar2, block.stripes2, &block.last_fill2, 0, false, theme::muted(), false);
            }
        } else {
            set_bar_remaining(block, 0, false);
            if (block.bar2) {
                anim_bar(block.bar2, block.stripes2, &block.last_fill2, 0, false, theme::muted(), false);
            }
        }
    } else {
        set_bar_remaining(block, 0, false);
    }
}

void paint_source(Block &block, const aitokens::Source *src, bool ready) {
    if (!block.value || !block.sub) return;
    if (!source_ok(src, ready)) {
        // Enabled path calls paint_source only when show_* is on — keep block visible.
        paint_offline(block, src, ready);
        return;
    }
    set_block_visible(block, true);

    if (block.dual) {
        int a = 0, b = 0;
        if (!parse_dual_pct(src->display, &a, &b)) {
            set_if(block.value, src->display);
            set_if(block.sub, "formato inválido");
            style_block_alert(block, 100, false);
            return;
        }
        char headline[32];
        if (block.dual_used) snprintf(headline, sizeof(headline), "%d%% · %d%%", a, b);
        else snprintf(headline, sizeof(headline), "%d%% · %d%% restante", a, b);
        set_if(block.value, headline);
        char r1[40], r2[40];
        if (block.dual_used) {
            snprintf(r1, sizeof(r1), "Modelos · %d%% usados", a);
            snprintf(r2, sizeof(r2), "Outros · %d%% usados", b);
            set_if(block.row1_label, r1);
            set_if(block.row2_label, r2);
            set_bar_used(block, block.bar, block.stripes, &block.last_fill, a, true);
            lv_color_t other_tone = bar_tone_used(block.brand, b);
            if (b < 100 - kWarnPct) other_tone = lv_color_mix(block.brand, theme::text_secondary(), LV_OPA_60);
            anim_bar(block.bar2, block.stripes2, &block.last_fill2, b, true, other_tone, b >= 100 - kCriticalPct);
            int rem = src->kind == aitokens::Kind::Pct ? static_cast<int>(src->value) : (100 - (a > b ? a : b));
            if (rem < 0) rem = 0;
            if (rem > 100) rem = 100;
            if (src->reset[0]) {
                char text[48];
                snprintf(text, sizeof(text), "usados · reseta %s", src->reset);
                set_if(block.sub, text);
            } else {
                set_if(block.sub, "usados no ciclo");
            }
            style_block_alert(block, rem, true);
        } else {
            // Codex: a=5h restante, b=semana restante (headline já tem "restante").
            snprintf(r1, sizeof(r1), "5 horas · %d%% restante", a);
            snprintf(r2, sizeof(r2), "Semana · %d%% restante", b);
            set_if(block.row1_label, r1);
            set_if(block.row2_label, r2);
            set_bar_remaining(block, a, true);
            anim_bar(block.bar2, block.stripes2, &block.last_fill2, b, true, bar_tone_remaining(block.brand, b),
                     b > 0 && b < kCriticalPct);
            int rem = src->kind == aitokens::Kind::Pct ? static_cast<int>(src->value) : a;
            if (rem < 0) rem = 0;
            if (rem > 100) rem = 100;
            if (src->reset[0]) {
                char text[48];
                snprintf(text, sizeof(text), "restante · 5 h reseta %s", src->reset);
                set_if(block.sub, text);
            } else {
                set_if(block.sub, "% restante");
            }
            style_block_alert(block, rem, true);
        }
        return;
    }

    set_if(block.value, src->display);
    int fill = 0;
    if (src->kind == aitokens::Kind::Pct) {
        fill = src->value;
        if (fill < 0) fill = 0;
        if (fill > 100) fill = 100;
        if (src->reset[0]) {
            char text[48];
            if (fill < kCriticalPct) snprintf(text, sizeof(text), "quase esgotado · reseta %s", src->reset);
            else if (fill < kWarnPct) snprintf(text, sizeof(text), "baixo · reseta %s", src->reset);
            else snprintf(text, sizeof(text), "reseta %s", src->reset);
            set_if(block.sub, text);
        } else if (fill < kCriticalPct) {
            set_if(block.sub, "quase esgotado");
        } else if (fill < kWarnPct) {
            set_if(block.sub, "baixo");
        } else {
            set_if(block.sub, "restante");
        }
    } else if (src->reset[0]) {
        char text[40];
        snprintf(text, sizeof(text), "reseta %s", src->reset);
        set_if(block.sub, text);
    } else if (src->kind == aitokens::Kind::Tokens) {
        set_if(block.sub, "últimas 24 h");
    } else {
        set_if(block.sub, "");
    }
    set_bar_remaining(block, fill, true);
    style_block_alert(block, fill, true);
}

lv_obj_t *standalone = nullptr;
lv_event_cb_t back_cb = nullptr;
lv_event_cb_t refresh_cb = nullptr;

void destroy_ui() {
    if (screen) {
        lv_obj_delete(screen);
        screen = nullptr;
    }
    body = nullptr;
    title_label = nullptr;
    status_label = nullptr;
    codex_block = {};
    claude_block = {};
    cursor_block = {};
    awaiting_refresh = false;
}

void build_ui(lv_obj_t *parent) {
    if (!parent) return;
    destroy_ui();
    // Rebuild into the slot — no long-lived reparent of a parked tree.
    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(screen, theme::background(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_pad_row(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    kit::SubsysBar bar =
        kit::subsys_bar(screen, LV_SYMBOL_LEFT, back_cb, palette::widget_aitokens(), refresh_cb, LV_SYMBOL_REFRESH);
    title_label = bar.title;
    status_label = bar.status;
    if (title_label) lv_label_set_text(title_label, "AI Tokens");
    set_status("");

    body = kit::bare(screen);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_height(body, 0);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, theme::kListPad, 0);
    lv_obj_set_style_pad_top(body, 4, 0);
    lv_obj_set_style_pad_bottom(body, theme::kListPad, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 10, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    codex_block = make_block(body, "Codex", aitokens_logos::codex(), true, false, brand_codex());
    claude_block = make_block(body, "Claude", aitokens_logos::claude(), false, false, brand_claude());
    cursor_block = make_block(body, "Cursor", aitokens_logos::cursor(), true, true, brand_cursor());
    set_block_visible(codex_block, false);
    set_block_visible(claude_block, false);
    set_block_visible(cursor_block, false);
    reset_bar_anim();
}

}  // namespace

void aitokens_page::begin(lv_event_cb_t on_back, lv_event_cb_t on_refresh) {
    back_cb = on_back;
    refresh_cb = on_refresh;
    // UI is built on attach/show (slot rebuild).
}

void aitokens_page::show() {
    if (!standalone) {
        standalone = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(standalone, theme::background(), 0);
        lv_obj_set_style_bg_opa(standalone, LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(standalone, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(standalone, 0, 0);
        lv_obj_remove_flag(standalone, LV_OBJ_FLAG_SCROLLABLE);
    }
    build_ui(standalone);
    paint(cached_view);
    lv_screen_load(standalone);
}

void aitokens_page::attach(lv_obj_t *parent) {
    if (!parent) {
        destroy_ui();
        return;
    }
    build_ui(parent);
    paint(cached_view);
}

void aitokens_page::show_refreshing() {
    // Stay on this screen and refresh the cards in place — never navigate home.
    if (standalone) lv_screen_load(standalone);
    else if (screen && !lv_obj_has_flag(screen, LV_OBJ_FLAG_HIDDEN)) {
        // Already mounted in detail_host — just update status.
    }
    awaiting_refresh = true;
    reset_bar_anim();
    set_status("atualizando…");
}

void aitokens_page::refresh_done() { awaiting_refresh = false; }

void aitokens_page::apply_theme() {
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, theme::background(), 0);
    if (title_label) lv_obj_set_style_text_color(title_label, theme::text_primary(), 0);
    if (status_label) lv_obj_set_style_text_color(status_label, theme::muted(), 0);
    auto restyle = [](Block &b) {
        if (!b.box) return;
        lv_obj_set_style_bg_color(b.box, theme::surface(), 0);
        if (b.dual) {
            int u1 = b.last_fill < 0 ? 0 : b.last_fill;
            int u2 = b.last_fill2 < 0 ? 0 : b.last_fill2;
            if (b.dual_used) {
                style_bar(b.bar, bar_tone_used(b.brand, u1));
                if (b.bar2) {
                    lv_color_t other = bar_tone_used(b.brand, u2);
                    if (u2 < 100 - kWarnPct) other = lv_color_mix(b.brand, theme::text_secondary(), LV_OPA_60);
                    style_bar(b.bar2, other);
                }
                int worst_used = u1 > u2 ? u1 : u2;
                style_block_alert(b, 100 - worst_used, b.last_fill >= 0);
            } else {
                style_bar(b.bar, bar_tone_remaining(b.brand, u1));
                if (b.bar2) style_bar(b.bar2, bar_tone_remaining(b.brand, u2));
                style_block_alert(b, u1, b.last_fill >= 0);
            }
            if (b.row1_label) lv_obj_set_style_text_color(b.row1_label, theme::text_secondary(), 0);
            if (b.row2_label) lv_obj_set_style_text_color(b.row2_label, theme::text_secondary(), 0);
        } else {
            style_bar(b.bar, bar_tone_remaining(b.brand, b.last_fill < 0 ? 0 : b.last_fill));
            style_block_alert(b, b.last_fill < 0 ? 100 : b.last_fill, b.last_fill >= 0);
        }
        if (b.sub) lv_obj_set_style_text_color(b.sub, theme::text_secondary(), 0);
    };
    restyle(codex_block);
    restyle(claude_block);
    restyle(cursor_block);
}

const aitokens::View &aitokens_page::last_view() { return cached_view; }

void aitokens_page::paint(const aitokens::View &view) {
    cached_view = view;
    DeviceSettings device = settings::load_device();
    if (device.show_aitokens_codex) paint_source(codex_block, aitokens::find(view, "codex"), view.ready);
    else set_block_visible(codex_block, false);
    if (device.show_aitokens_claude) paint_source(claude_block, aitokens::find(view, "claude"), view.ready);
    else set_block_visible(claude_block, false);
    if (device.show_aitokens_cursor) paint_source(cursor_block, aitokens::find(view, "cursor"), view.ready);
    else set_block_visible(cursor_block, false);

    const bool any_ok = (device.show_aitokens_codex && source_ok(aitokens::find(view, "codex"), view.ready)) ||
                        (device.show_aitokens_claude && source_ok(aitokens::find(view, "claude"), view.ready)) ||
                        (device.show_aitokens_cursor && source_ok(aitokens::find(view, "cursor"), view.ready));
    const bool any_shown =
        device.show_aitokens_codex || device.show_aitokens_claude || device.show_aitokens_cursor;
    if (awaiting_refresh) {
        set_status("atualizando…");
        return;
    }
    const char *status = "";
    if (!view.ready) status = "ligar Bridge";
    else if (!any_shown) status = "nenhuma fonte";
    else if (!any_ok) status = "sem cotas agora";
    else if (view.stale) status = "desatualizado";
    set_status(status);
}
