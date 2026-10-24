#include "ui/settings_panel.h"

#include "platform/device_resources.h"
#include "ui/kit.h"
#include "ui/palette.h"
#include "ui/theme.h"
#include "ui/ts_kit.h"

#include <Arduino.h>

#include <cstring>

#include <src/libs/qrcode/qrcodegen.h>

#include <esp_heap_caps.h>

#include <cstdio>

namespace {
enum class Field : uint8_t {
    Orientation,
    Sound,
    Brightness,
    Sync,
    Appearance,
    Rest,
    Clock,
    AiCodex,
    AiClaude,
    AiCursor,
};
enum class Page : uint8_t { Hub, General, Resources, Todoist, Weather, AiTokens, Agent };

struct Choice {
    Field field;
    int value;
};

constexpr Choice kOrientation[] = {{Field::Orientation, 1}, {Field::Orientation, 0}};
constexpr const char *kOrientationText[] = {"Vertical", "Horizontal"};
constexpr Choice kSound[] = {{Field::Sound, 1}, {Field::Sound, 0}};
constexpr const char *kSoundText[] = {"Ligado", "Desligado"};
constexpr Choice kBrightness[] = {{Field::Brightness, 30}, {Field::Brightness, 60}, {Field::Brightness, 100}};
constexpr const char *kBrightnessText[] = {"Baixo", "Médio", "Alto"};
constexpr Choice kSync[] = {{Field::Sync, 5}, {Field::Sync, 15}, {Field::Sync, 30}, {Field::Sync, 60}};
constexpr const char *kSyncText[] = {"5 min", "15 min", "30 min", "1 h"};
constexpr Choice kAppearance[] = {{Field::Appearance, 0}, {Field::Appearance, 1}, {Field::Appearance, 2}};
constexpr const char *kAppearanceText[] = {"Claro", "Escuro", "Auto"};
constexpr const char *kRestText[] = {"2 min", "5 min", "10 min", "20 min", "30 min", "Nunca"};
constexpr const char *kClockText[] = {"1 min", "2 min", "5 min", "10 min", "Até tocar"};

lv_obj_t *root = nullptr;
lv_obj_t *body = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *qr_layer = nullptr;
uint8_t *qr_pixels = nullptr;
lv_image_dsc_t qr_image;
settings_panel::ChangeFn on_change = nullptr;
settings_panel::DismissFn on_dismiss = nullptr;
settings_panel::RequestAiTokensFn on_request_aitokens = nullptr;
Page page = Page::Hub;

void release_qr_pixels() {
    if (!qr_pixels) return;
    heap_caps_free(qr_pixels);
    qr_pixels = nullptr;
}

// Drawn as a plain RGB565 image. The LVGL QR widget paints a 1-bit canvas, and that path locked the panel.
bool paint_qr(const char *url) {
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
    uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
    if (!qrcodegen_encodeText(url, temp, code, qrcodegen_Ecc_MEDIUM, 1, 4, qrcodegen_Mask_AUTO, true)) return false;
    int modules = qrcodegen_getSize(code);
    int scale = 168 / (modules + 4);
    if (scale < 4) scale = 4;
    int side = (modules + 4) * scale;
    release_qr_pixels();
    qr_pixels = static_cast<uint8_t *>(
        heap_caps_malloc(static_cast<size_t>(side) * side * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!qr_pixels) return false;
    auto *px = reinterpret_cast<uint16_t *>(qr_pixels);
    for (int y = 0; y < side; ++y) {
        int row = y / scale - 2;
        for (int x = 0; x < side; ++x) {
            int col = x / scale - 2;
            bool dark = col >= 0 && row >= 0 && col < modules && row < modules && qrcodegen_getModule(code, col, row);
            px[y * side + x] = dark ? 0x0000 : 0xFFFF;
        }
    }
    qr_image = {};
    qr_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    qr_image.header.cf = LV_COLOR_FORMAT_RGB565;
    qr_image.header.w = static_cast<uint32_t>(side);
    qr_image.header.h = static_cast<uint32_t>(side);
    qr_image.header.stride = static_cast<uint32_t>(side * 2);
    qr_image.data_size = static_cast<uint32_t>(side) * side * 2;
    qr_image.data = qr_pixels;
    return true;
}
DeviceSettings values;
DeviceInfo info = {};
aitokens::View aitokens_view = {};
agent::View agent_view = {};
bool agent_pack_on_sd = false;

void on_pedir_cotas(lv_event_t *) {
    if (on_request_aitokens) on_request_aitokens();
}

void fill_agent_status() {
    lv_obj_t *status = lv_label_create(content);
    const char *text = "Aguardando agente no Mac…";
    if (agent_view.ready && agent_view.ok) text = "OK — comunicando via USB";
    else if (agent_view.ready) text = agent_view.detail[0] ? agent_view.detail : "Falha";
    lv_label_set_text(status, text);
    lv_obj_set_width(status, lv_pct(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(status, &theme::title_font(), 0);
    lv_obj_set_style_text_color(status, theme::text_primary(), 0);

    lv_obj_t *pack = lv_label_create(content);
    lv_label_set_text(pack, agent_pack_on_sd ? "Pacote no SD: pronto (agent.tgz)."
                                             : "Pacote no SD: ausente — envie agent.tgz pelo site.");
    lv_obj_set_width(pack, lv_pct(100));
    lv_label_set_long_mode(pack, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(pack, &theme::body_font(), 0);
    lv_obj_set_style_text_color(pack, theme::text_primary(), 0);

    lv_obj_t *note = lv_label_create(content);
    lv_label_set_text(note,
                      "Instale pelo site no notebook (copiar comando no Terminal). O pacote fica no storage do painel.");
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &theme::small_font(), 0);
    lv_obj_set_style_text_color(note, theme::text_secondary(), 0);
}

void fill_aitokens_status();  // after choices()

int current(Field field) {
    switch (field) {
        case Field::Orientation: return values.portrait ? 1 : 0;
        case Field::Sound: return values.sound ? 1 : 0;
        case Field::Brightness: return values.brightness;
        case Field::Sync: return values.sync_minutes;
        case Field::Appearance: return static_cast<int>(values.appearance);
        case Field::Rest: return values.rest_minutes;
        case Field::Clock: return values.clock_minutes;
        case Field::AiCodex: return values.show_aitokens_codex ? 1 : 0;
        case Field::AiClaude: return values.show_aitokens_claude ? 1 : 0;
        case Field::AiCursor: return values.show_aitokens_cursor ? 1 : 0;
    }
    return 0;
}

void fill();

void on_choice(lv_event_t *event) {
    const Choice *choice = static_cast<const Choice *>(lv_event_get_user_data(event));
    DeviceSettings next = values;
    switch (choice->field) {
        case Field::Orientation: next.portrait = choice->value == 1; break;
        case Field::Sound: next.sound = choice->value == 1; break;
        case Field::Brightness: next.brightness = clamp_brightness(choice->value); break;
        case Field::Sync: next.sync_minutes = clamp_sync(choice->value); break;
        case Field::Appearance: next.appearance = clamp_appearance(choice->value); break;
        case Field::Rest: next.rest_minutes = clamp_rest_minutes(choice->value); break;
        case Field::Clock: next.clock_minutes = clamp_clock_minutes(choice->value); break;
        case Field::AiCodex: next.show_aitokens_codex = choice->value == 1; break;
        case Field::AiClaude: next.show_aitokens_claude = choice->value == 1; break;
        case Field::AiCursor: next.show_aitokens_cursor = choice->value == 1; break;
    }
    values = next;
    lv_async_call([](void *) { fill(); }, nullptr);
    if (on_change) on_change(next);
}

void on_close(lv_event_t *) {
    settings_panel::DismissFn dismiss = on_dismiss;
    settings_panel::close();
    if (dismiss) dismiss();
}

void on_nav(lv_event_t *event) {
    page = static_cast<Page>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    fill();
}

// Um só controle no topo: subpágina → hub; hub → home.
void on_header_back(lv_event_t *e) {
    if (page != Page::Hub) {
        page = Page::Hub;
        fill();
        return;
    }
    on_close(e);
}

void close_qr(lv_event_t *) {
    if (!qr_layer) return;
    lv_obj_delete(qr_layer);
    qr_layer = nullptr;
    release_qr_pixels();
}

void on_qr(lv_event_t *) {
    if (qr_layer || !root || !info.ip[0]) return;
    qr_layer = kit::bare(root);
    lv_obj_add_flag(qr_layer, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(qr_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(qr_layer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(qr_layer, theme::paper(), 0);
    lv_obj_set_style_bg_opa(qr_layer, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(qr_layer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(qr_layer, 0, 0);

    lv_obj_t *stripe = kit::bare(qr_layer);
    lv_obj_set_size(stripe, lv_pct(100), 4);
    lv_obj_set_style_bg_color(stripe, theme::primary(), 0);
    lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);

    lv_obj_t *header = kit::bare(qr_layer);
    lv_obj_set_size(header, lv_pct(100), 48);
    lv_obj_set_style_pad_hor(header, 10, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "No celular");
    lv_obj_set_style_text_font(title, &theme::title_font(), 0);
    kit::action_button(header, "Fechar", close_qr);

    lv_obj_t *center = kit::bare(qr_layer);
    lv_obj_set_width(center, lv_pct(100));
    lv_obj_set_flex_grow(center, 1);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(center, 12, 0);

    char url[64];
    snprintf(url, sizeof(url), "http://%s/", info.ip);
    if (paint_qr(url)) {
        lv_obj_t *code = lv_image_create(center);
        lv_image_set_src(code, &qr_image);
    } else {
        lv_obj_t *failed = lv_label_create(center);
        lv_label_set_text(failed, "Não foi possível gerar o código");
        lv_obj_set_style_text_color(failed, theme::muted(), 0);
    }

    lv_obj_t *hint = lv_label_create(center);
    lv_label_set_text(hint, "Aponte a câmera do celular");
    lv_obj_set_style_text_font(hint, &theme::label_font(), 0);
    lv_obj_set_style_text_color(hint, theme::ink(), 0);
}

lv_obj_t *make_card() {
    lv_obj_t *card = kit::bare(content);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme::surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, theme::border(), 0);
    lv_obj_set_style_pad_hor(card, 12, 0);
    lv_obj_set_style_pad_ver(card, 6, 0);
    lv_obj_set_style_margin_top(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 4, 0);
    // Não rolar o card — o toque deve subir para o body (Memória/Geral).
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return card;
}

void card_title(lv_obj_t *card, const char *title) {
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &theme::small_font(), 0);
    lv_obj_set_style_text_color(label, theme::text_secondary(), 0);
}

lv_obj_t *chip_row(lv_obj_t *card) {
    lv_obj_t *row = kit::bare(card);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_pad_row(row, 6, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN);
    return row;
}

lv_obj_t *section(const char *title) {
    lv_obj_t *card = make_card();
    card_title(card, title);
    return chip_row(card);
}

template <size_t N>
void fill_chips(lv_obj_t *row, const Choice (&options)[N], const char *const (&texts)[N], uint32_t tone = 0) {
    int selected = current(options[0].field);
    // Brightness is stored as a percentage; highlight the closest preset.
    int best = 0;
    for (size_t i = 0; i < N; ++i) {
        int gap = options[i].value - selected;
        int best_gap = options[best].value - selected;
        if ((gap < 0 ? -gap : gap) < (best_gap < 0 ? -best_gap : best_gap)) best = static_cast<int>(i);
    }
    for (size_t i = 0; i < N; ++i) {
        lv_obj_t *chip =
            tessara::ui::TsChip(row, texts[i], static_cast<int>(i) == best, on_choice, const_cast<Choice *>(&options[i]));
        if (tone) {
            tessara::ui::TsSetChipTone(chip, tone);
            tessara::ui::TsSetChipSelected(chip, static_cast<int>(i) == best);
        }
    }
}

template <size_t N>
void choices(const char *title, const Choice (&options)[N], const char *const (&texts)[N], uint32_t tone = 0) {
    fill_chips(section(title), options, texts, tone);
}

void fill_aitokens_status() {
    lv_obj_t *updated = lv_label_create(content);
    char line[64];
    if (!aitokens_view.ready) {
        snprintf(line, sizeof(line), "Última atualização: ainda não");
    } else if (aitokens_view.stale) {
        snprintf(line, sizeof(line), "Última atualização: desatualizado");
    } else {
        uint32_t age_s = (millis() - aitokens_view.received_ms) / 1000u;
        if (age_s < 60) snprintf(line, sizeof(line), "Última atualização: há %u s", static_cast<unsigned>(age_s));
        else snprintf(line, sizeof(line), "Última atualização: há %u min", static_cast<unsigned>(age_s / 60u));
    }
    lv_label_set_text(updated, line);
    lv_obj_set_width(updated, lv_pct(100));
    lv_label_set_long_mode(updated, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(updated, &theme::body_font(), 0);
    lv_obj_set_style_text_color(updated, theme::text_primary(), 0);

    static constexpr Choice kAiCodex[] = {{Field::AiCodex, 1}, {Field::AiCodex, 0}};
    static constexpr Choice kAiClaude[] = {{Field::AiClaude, 1}, {Field::AiClaude, 0}};
    static constexpr Choice kAiCursor[] = {{Field::AiCursor, 1}, {Field::AiCursor, 0}};
    static constexpr const char *kOnOff[] = {"Exibir", "Ocultar"};
    choices("Codex na home", kAiCodex, kOnOff, palette::primary());
    choices("Claude na home", kAiClaude, kOnOff, palette::primary());
    choices("Cursor na home", kAiCursor, kOnOff, palette::primary());
    kit::action_button(content, "Pedir cotas", on_pedir_cotas);
    lv_obj_t *foot = lv_label_create(content);
    lv_label_set_text(foot, "Barras detalhadas ficam no widget da home.");
    lv_obj_set_style_text_font(foot, &theme::small_font(), 0);
    lv_obj_set_style_text_color(foot, theme::text_secondary(), 0);
}

struct RangeOpts {
    Field field;
    const int *values;
    const char *const *texts;
    int count;
};

void on_range(lv_event_t *event) {
    auto *opts = static_cast<RangeOpts *>(lv_event_get_user_data(event));
    if (!opts || opts->count <= 0) return;
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
    int idx = static_cast<int>(lv_slider_get_value(slider));
    if (idx < 0) idx = 0;
    if (idx >= opts->count) idx = opts->count - 1;
    // wrap → head → value label (second child)
    lv_obj_t *wrap = lv_obj_get_parent(slider);
    lv_obj_t *head = wrap ? lv_obj_get_child(wrap, 0) : nullptr;
    lv_obj_t *value_label = head ? lv_obj_get_child(head, 1) : nullptr;
    if (value_label) lv_label_set_text(value_label, opts->texts[idx]);

    DeviceSettings next = values;
    switch (opts->field) {
        case Field::Rest: next.rest_minutes = clamp_rest_minutes(opts->values[idx]); break;
        case Field::Clock: next.clock_minutes = clamp_clock_minutes(opts->values[idx]); break;
        default: return;
    }
    values = next;
    if (on_change) on_change(next);
}

int nearest_index(const int *values, int count, int selected) {
    int best = 0;
    for (int i = 0; i < count; ++i) {
        int gap = values[i] - selected;
        int best_gap = values[best] - selected;
        if ((gap < 0 ? -gap : gap) < (best_gap < 0 ? -best_gap : best_gap)) best = i;
    }
    return best;
}

void range_field(lv_obj_t *card, const char *label, Field field, const int *vals, const char *const *texts, int count,
                 RangeOpts *storage) {
    *storage = RangeOpts{field, vals, texts, count};
    int selected = current(field);
    int idx = nearest_index(vals, count, selected);

    lv_obj_t *wrap = kit::bare(card);
    lv_obj_set_width(wrap, lv_pct(100));
    lv_obj_set_height(wrap, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wrap, 4, 0);
    lv_obj_add_flag(wrap, LV_OBJ_FLAG_SCROLL_CHAIN);

    lv_obj_t *head = kit::bare(wrap);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(head, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_t *name = lv_label_create(head);
    lv_label_set_text(name, label);
    lv_obj_set_style_text_font(name, &theme::small_font(), 0);
    lv_obj_set_style_text_color(name, theme::text_secondary(), 0);
    lv_obj_t *value = lv_label_create(head);
    lv_label_set_text(value, texts[idx]);
    lv_obj_set_style_text_font(value, &theme::label_font(), 0);
    lv_obj_set_style_text_color(value, theme::text_primary(), 0);

    lv_obj_t *slider = lv_slider_create(wrap);
    lv_obj_set_width(slider, lv_pct(100));
    lv_obj_set_height(slider, 22);
    lv_slider_set_range(slider, 0, count - 1);
    lv_slider_set_value(slider, idx, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, theme::hairline(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 999, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, theme::primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 999, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, theme::primary(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 10);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(slider, on_range, LV_EVENT_VALUE_CHANGED, storage);
}

// One card: when the flip clock appears + how long it stays before backlight off.
void rest_block() {
    static RangeOpts rest_opts;
    static RangeOpts clock_opts;
    static constexpr int kRestVals[] = {2, 5, 10, 20, 30, 0};
    static constexpr int kClockVals[] = {1, 2, 5, 10, 0};

    lv_obj_t *card = make_card();
    card_title(card, "Descanso de tela");
    range_field(card, "Aparece após inatividade", Field::Rest, kRestVals, kRestText,
                static_cast<int>(sizeof(kRestVals) / sizeof(kRestVals[0])), &rest_opts);
    range_field(card, "Relógio fica por", Field::Clock, kClockVals, kClockText,
                static_cast<int>(sizeof(kClockVals) / sizeof(kClockVals[0])), &clock_opts);
    lv_obj_t *note = lv_label_create(card);
    lv_label_set_text(note, "Depois do tempo do relógio, a tela apaga. “Até tocar” mantém o flip clock.");
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &theme::small_font(), 0);
    lv_obj_set_style_text_color(note, theme::text_secondary(), 0);
}

void resource_meter(lv_obj_t *card, const char *title, const char *value, const char *extra, int used_pct) {
    lv_obj_t *row = kit::bare(card);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 2, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, title);
    lv_obj_set_style_text_font(name, &theme::small_font(), 0);
    lv_obj_set_style_text_color(name, theme::text_secondary(), 0);
    lv_obj_t *detail = lv_label_create(row);
    lv_label_set_text(detail, value);
    lv_obj_set_width(detail, lv_pct(100));
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(detail, &theme::body_font(), 0);
    lv_obj_set_style_text_color(detail, theme::text_primary(), 0);
    if (extra && extra[0]) {
        lv_obj_t *sub = lv_label_create(row);
        lv_label_set_text(sub, extra);
        lv_obj_set_width(sub, lv_pct(100));
        lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(sub, &theme::small_font(), 0);
        lv_obj_set_style_text_color(sub, theme::text_secondary(), 0);
    }
    if (used_pct < 0) return;
    if (used_pct > 100) used_pct = 100;
    lv_obj_t *bar = lv_bar_create(row);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 10);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, used_pct, LV_ANIM_OFF);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_radius(bar, 999, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 999, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, theme::hairline(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_color_t fill = used_pct >= 90 ? theme::error() : used_pct >= 70 ? theme::warning() : theme::primary();
    lv_obj_set_style_bg_color(bar, fill, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
}

void fill_resources() {
    device_resources::Sample r = device_resources::sample();
    lv_obj_t *card = make_card();
    card_title(card, "Memória");

    char heap_pair[40], heap_block[32], heap_min[32], heap_extra[72];
    device_resources::format_pair(r.heap_free_b, r.heap_total_b, heap_pair, sizeof(heap_pair));
    device_resources::format_bytes(r.heap_largest_b, heap_block, sizeof(heap_block));
    device_resources::format_bytes(r.heap_min_b, heap_min, sizeof(heap_min));
    snprintf(heap_extra, sizeof(heap_extra), "Maior bloco %s · mínimo histórico %s", heap_block, heap_min);
    resource_meter(card, "DRAM interna", heap_pair, heap_extra,
                   device_resources::percent_used(r.heap_free_b, r.heap_total_b));

    char psram_pair[40], psram_block[32], psram_extra[48];
    if (r.psram_total_b == 0) {
        resource_meter(card, "PSRAM", "indisponível", nullptr, -1);
    } else {
        device_resources::format_pair(r.psram_free_b, r.psram_total_b, psram_pair, sizeof(psram_pair));
        device_resources::format_bytes(r.psram_largest_b, psram_block, sizeof(psram_block));
        snprintf(psram_extra, sizeof(psram_extra), "Maior bloco %s", psram_block);
        resource_meter(card, "PSRAM", psram_pair, psram_extra,
                       device_resources::percent_used(r.psram_free_b, r.psram_total_b));
    }

    lv_obj_t *flash = make_card();
    card_title(flash, "Firmware (flash)");
    char sketch[40], sketch_free[32], flash_line[80];
    device_resources::format_bytes(r.flash_sketch_b, sketch, sizeof(sketch));
    device_resources::format_bytes(r.flash_sketch_free_b, sketch_free, sizeof(sketch_free));
    snprintf(flash_line, sizeof(flash_line), "%s da imagem · %s livres (OTA)", sketch, sketch_free);
    uint64_t sketch_cap = static_cast<uint64_t>(r.flash_sketch_b) + r.flash_sketch_free_b;
    int flash_pct = sketch_cap ? device_resources::percent_used(r.flash_sketch_free_b, sketch_cap) : -1;
    resource_meter(flash, "Imagem", flash_line, nullptr, flash_pct);

    lv_obj_t *store = make_card();
    card_title(store, "Armazenamento");
    if (!r.storage_ready || r.storage_total_b == 0) {
        resource_meter(store, "Volume", "nenhum montado", nullptr, -1);
    } else {
        char pair[40];
        device_resources::format_pair(r.storage_free_b, r.storage_total_b, pair, sizeof(pair));
        resource_meter(store, r.storage_kind[0] ? r.storage_kind : "Volume", pair, nullptr,
                       device_resources::percent_used(r.storage_free_b, r.storage_total_b));
    }

    lv_obj_t *up = make_card();
    card_title(up, "Sistema");
    char uptime[48];
    uint32_t s = r.uptime_s;
    if (s < 60) snprintf(uptime, sizeof(uptime), "%u s", static_cast<unsigned>(s));
    else if (s < 3600) snprintf(uptime, sizeof(uptime), "%u min", static_cast<unsigned>(s / 60u));
    else snprintf(uptime, sizeof(uptime), "%u h %u min", static_cast<unsigned>(s / 3600u),
                  static_cast<unsigned>((s % 3600u) / 60u));
    resource_meter(up, "Ligado há", uptime, nullptr, -1);

    lv_obj_t *note = lv_label_create(content);
    lv_label_set_text(note, "Valores ao vivo. Maior bloco interno importa para TLS/Wi‑Fi. Arraste para rolar.");
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &theme::small_font(), 0);
    lv_obj_set_style_text_color(note, theme::text_secondary(), 0);
}

void connection_card() {
    lv_obj_t *card = kit::bare(content);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme::surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_ver(card, 8, 0);
    lv_obj_set_style_pad_hor(card, 12, 0);
    lv_obj_set_style_margin_top(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, 10, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *text = kit::bare(card);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 1, 0);

    bool online = info.online && info.ssid[0];
    lv_obj_t *name = lv_label_create(text);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, lv_pct(100));
    lv_label_set_text(name, online ? info.ssid : "Sem Wi-Fi");
    lv_obj_set_style_text_font(name, &theme::body_font(), 0);
    lv_obj_set_style_text_color(name, theme::text_primary(), 0);

    lv_obj_t *detail = lv_label_create(text);
    if (online && info.ip[0]) {
        char line[48];
        snprintf(line, sizeof(line), "%s · %s", info.ip, wifi_strength_label(wifi_strength(true, info.rssi)));
        lv_label_set_text(detail, line);
    } else {
        lv_label_set_text(detail, "Aguardando a rede");
    }
    lv_obj_set_style_text_font(detail, &theme::small_font(), 0);
    lv_obj_set_style_text_color(detail, theme::text_secondary(), 0);

    lv_obj_t *button = kit::action_button(card, info.ip[0] ? "QR Code" : "Sem rede", on_qr);
    if (!info.ip[0]) {
        lv_obj_set_style_bg_color(button, theme::hairline(), 0);
        lv_obj_t *label = lv_obj_get_child(button, 0);
        if (label) lv_obj_set_style_text_color(label, theme::muted(), 0);
    }
}

void link_row(const char *symbol, uint32_t tone, const char *name, Page dest) {
    lv_obj_t *row = kit::bare(content);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 48);
    lv_obj_set_style_bg_color(row, theme::surface(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, theme::border(), 0);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(row, on_nav, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(dest)));

    lv_obj_t *glyph = lv_label_create(row);
    lv_label_set_text(glyph, symbol);
    lv_obj_set_style_text_font(glyph, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(glyph, lv_color_hex(tone), 0);

    lv_obj_t *title = lv_label_create(row);
    lv_label_set_text(title, name);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_font(title, &theme::label_font(), 0);
    lv_obj_set_style_text_color(title, theme::text_primary(), 0);

    lv_obj_t *chevron = lv_label_create(row);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(chevron, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(chevron, theme::text_secondary(), 0);
}

void fill() {
    if (!body) return;
    if (root) lv_obj_set_style_bg_color(root, theme::background(), 0);
    lv_obj_t *header = root ? lv_obj_get_child(root, 0) : nullptr;
    // header: [Voltar] [título]
    lv_obj_t *back = header ? lv_obj_get_child(header, 0) : nullptr;
    lv_obj_t *title = header ? lv_obj_get_child(header, 1) : nullptr;
    if (title) {
        const char *heading = "Configurações";
        if (page == Page::General) heading = "Geral";
        else if (page == Page::Resources) heading = "Memória";
        else if (page == Page::Todoist) heading = "Todoist";
        else if (page == Page::Weather) heading = "Clima";
        else if (page == Page::AiTokens) heading = "AI Tokens";
        else if (page == Page::Agent) heading = "Agente Tessara";
        lv_label_set_text(title, heading);
        lv_obj_set_style_text_color(title, theme::text_primary(), 0);
    }
    if (back) {
        lv_obj_set_style_bg_color(back, theme::primary(), 0);
        lv_obj_t *label = lv_obj_get_child(back, 0);
        if (label) lv_obj_set_style_text_color(label, theme::on_primary(), 0);
    }
    lv_obj_clean(body);

    content = body;
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    // kit::bare tira SCROLLABLE e CLICKABLE — sem CLICKABLE o LVGL não entrega o
    // arraste ao body e Memória/Geral não rolam no touch.
    lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_snap_y(content, LV_SCROLL_SNAP_NONE);
    lv_obj_set_style_pad_bottom(content, 32, 0);

    if (page == Page::Hub) {
        link_row(LV_SYMBOL_SETTINGS, palette::primary(), "Geral", Page::General);
        link_row(LV_SYMBOL_SD_CARD, palette::primary(), "Memória e armazenamento", Page::Resources);
        link_row(LV_SYMBOL_USB, palette::primary(), "Agente Tessara", Page::Agent);
        link_row(LV_SYMBOL_LIST, palette::widget_todoist(), "Todoist", Page::Todoist);
        link_row(LV_SYMBOL_TINT, palette::widget_weather(), "Clima", Page::Weather);
        link_row(LV_SYMBOL_CHARGE, palette::widget_aitokens(), "AI Tokens", Page::AiTokens);
        return;
    }
    if (page == Page::General) {
        connection_card();
        choices("Orientação", kOrientation, kOrientationText);
        choices("Som ao concluir", kSound, kSoundText);
        choices("Brilho", kBrightness, kBrightnessText);
        rest_block();
        choices("Aparência", kAppearance, kAppearanceText);
        return;
    }
    if (page == Page::Resources) {
        fill_resources();
        return;
    }
    if (page == Page::Agent) {
        fill_agent_status();
        return;
    }
    if (page == Page::AiTokens) {
        fill_aitokens_status();
        return;
    }
    if (page == Page::Weather) {
        lv_obj_t *note = lv_label_create(content);
        lv_label_set_text(note, "O local do Clima se define no celular ou notebook. Sem GPS neste aparelho.");
        lv_obj_set_width(note, lv_pct(100));
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(note, &theme::body_font(), 0);
        lv_obj_set_style_text_color(note, theme::text_primary(), 0);
        kit::action_button(content, info.ip[0] ? "QR Code" : "Sem rede", on_qr);
        return;
    }
    // Verde Tessara nas configs — o vermelho da marca fica só no widget/home.
    choices("Atualizar listas a cada", kSync, kSyncText, palette::primary());
    lv_obj_t *note = lv_label_create(content);
    lv_label_set_text(note, "Listas e token deste widget ficam na página do celular.");
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &theme::small_font(), 0);
    lv_obj_set_style_text_color(note, theme::text_secondary(), 0);
    kit::action_button(content, info.ip[0] ? "QR Code" : "Sem rede", on_qr);
}

void build() {
    root = kit::bare(lv_layer_top());
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(root, theme::background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *header = kit::bare(root);
    lv_obj_set_size(header, lv_pct(100), 48);
    lv_obj_set_style_pad_hor(header, 10, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 10, 0);
    kit::action_button(header, "Voltar", on_header_back);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Configurações");
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_font(title, &theme::title_font(), 0);
    lv_obj_set_style_text_color(title, theme::text_primary(), 0);

    body = kit::bare(root);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_height(body, 0);  // with flex_grow, fills leftover and can scroll
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_ACTIVE);
    fill();
}
}  // namespace

void settings_panel::open(ChangeFn callback, DismissFn dismiss, RequestAiTokensFn request_aitokens) {
    on_change = callback;
    on_dismiss = dismiss;
    on_request_aitokens = request_aitokens;
    if (!root) build();
}

void settings_panel::show_values(const DeviceSettings &settings, const DeviceInfo &device) {
    values = settings;
    info = device;
    fill();
}

void settings_panel::show_aitokens(const aitokens::View &view) {
    aitokens_view = view;
    if (root && (page == Page::AiTokens || page == Page::Hub || page == Page::Agent)) fill();
}

void settings_panel::show_agent(const agent::View &view, bool pack_on_sd) {
    agent_view = view;
    agent_pack_on_sd = pack_on_sd;
    if (root && (page == Page::Agent || page == Page::Hub)) fill();
}

void settings_panel::close() {
    if (!root) return;
    lv_obj_delete(root);
    root = body = content = qr_layer = nullptr;
    page = Page::Hub;
    release_qr_pixels();
}

bool settings_panel::visible() { return root != nullptr; }

void settings_panel::relayout() {
    if (!root) return;
    close();
    build();
}
