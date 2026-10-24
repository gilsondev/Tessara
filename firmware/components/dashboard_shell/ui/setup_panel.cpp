#include "ui/setup_panel.h"

#include "ui/kit.h"
#include "ui/theme.h"

#include <src/libs/qrcode/qrcodegen.h>

#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>

namespace {
lv_obj_t *root = nullptr;
lv_obj_t *token_root = nullptr;
uint8_t *qr_pixels = nullptr;
lv_image_dsc_t qr_image;
char title_text[40];
char headline_text[64];
char name_text[64];
char hint_text[96];
char url_text[64];

void keep(char *dest, size_t cap, const char *src) {
    if (!src) src = "";
    strncpy(dest, src, cap - 1);
    dest[cap - 1] = '\0';
}

void release_qr() {
    if (!qr_pixels) return;
    heap_caps_free(qr_pixels);
    qr_pixels = nullptr;
}

void release_if_idle() {
    if (!root && !token_root) release_qr();
}

// Plain RGB565. The LVGL QR widget's 1-bit canvas locked the panel.
bool paint_qr(const char *url, int max_side) {
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
    uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
    if (!qrcodegen_encodeText(url, temp, code, qrcodegen_Ecc_MEDIUM, 1, 4, qrcodegen_Mask_AUTO, true)) return false;
    int modules = qrcodegen_getSize(code);
    int scale = max_side / (modules + 4);
    if (scale < 3) scale = 3;
    int side = (modules + 4) * scale;
    release_qr();
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

lv_obj_t *wrapped(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

void build() {
    root = kit::bare(lv_layer_top());
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(root, theme::paper(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(root, 20, 0);
    lv_obj_set_style_pad_row(root, 8, 0);

    lv_obj_t *stripe = kit::bare(root);
    lv_obj_add_flag(stripe, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(stripe, lv_pct(100), 4);
    lv_obj_align(stripe, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(stripe, theme::primary(), 0);
    lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);

    wrapped(root, title_text, &theme::title_font(), theme::ink());
    wrapped(root, headline_text, &theme::label_font(), theme::muted());
    wrapped(root, name_text, &theme::title_font(), theme::primary());
    wrapped(root, hint_text, &theme::small_font(), theme::muted());

    int max_side = lv_display_get_vertical_resolution(nullptr) < 360 ? 112 : 156;
    if (paint_qr(url_text, max_side)) {
        lv_obj_t *code = lv_image_create(root);
        lv_image_set_src(code, &qr_image);
        lv_obj_set_style_margin_top(code, 6, 0);
    }
}

void build_token() {
    token_root = kit::bare(lv_screen_active());
    lv_obj_set_width(token_root, lv_pct(100));
    lv_obj_set_flex_grow(token_root, 1);
    lv_obj_set_flex_flow(token_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(token_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(token_root, 18, 0);
    lv_obj_set_style_pad_top(token_root, 6, 0);
    lv_obj_set_style_pad_bottom(token_root, 12, 0);
    lv_obj_set_style_pad_row(token_root, 8, 0);
    lv_obj_set_scroll_dir(token_root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(token_root, LV_SCROLLBAR_MODE_OFF);

    wrapped(token_root, "Falta o token", &theme::title_font(), theme::ink());
    wrapped(token_root, "O Wi-Fi já está conectado. As tarefas entram depois do token.", &theme::small_font(), theme::muted());
    wrapped(token_root, "No celular, na mesma rede:\n1. Aponte a câmera para o QR Code\n2. Foto, Configurações, Integrações, Desenvolvedor\n3. Copie o token da API e cole na página",
            &theme::small_font(), theme::ink());

    int max_side = lv_display_get_vertical_resolution(nullptr) < 400 ? 96 : 128;
    if (paint_qr(url_text, max_side)) {
        lv_obj_t *code = lv_image_create(token_root);
        lv_image_set_src(code, &qr_image);
        lv_obj_set_style_margin_top(code, 4, 0);
    }
    wrapped(token_root, url_text, &theme::small_font(), theme::primary());
}
}  // namespace

void setup_panel::show(const char *title, const char *headline, const char *name, const char *hint, const char *url) {
    keep(title_text, sizeof(title_text), title);
    keep(headline_text, sizeof(headline_text), headline);
    keep(name_text, sizeof(name_text), name);
    keep(hint_text, sizeof(hint_text), hint);
    keep(url_text, sizeof(url_text), url);
    hide_token();
    hide();
    build();
}

void setup_panel::hide() {
    if (!root) return;
    lv_obj_delete(root);
    root = nullptr;
    release_if_idle();
}

void setup_panel::show_token(const char *url) {
    keep(url_text, sizeof(url_text), url);
    hide();
    hide_token();
    build_token();
}

void setup_panel::hide_token() {
    if (!token_root) return;
    lv_obj_delete(token_root);
    token_root = nullptr;
    release_if_idle();
}

bool setup_panel::token_visible() { return token_root != nullptr; }

bool setup_panel::visible() { return root != nullptr || token_root != nullptr; }

void setup_panel::relayout() {
    bool full = root != nullptr;
    bool token = token_root != nullptr;
    if (full) {
        lv_obj_delete(root);
        root = nullptr;
    }
    if (token) {
        lv_obj_delete(token_root);
        token_root = nullptr;
    }
    release_qr();
    if (full) build();
    if (token) build_token();
}
