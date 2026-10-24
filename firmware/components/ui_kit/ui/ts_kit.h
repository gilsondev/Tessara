#pragma once

#include "ui/kit.h"

// Ts UI Kit — Terrara-facing aliases over kit::. Widgets and shell may include this
// instead of talking to kit:: directly; behavior is identical.
namespace tessara {
namespace ui {

inline lv_obj_t *TsBare(lv_obj_t *parent) { return kit::bare(parent); }
inline lv_obj_t *TsButton(lv_obj_t *parent, const char *text, lv_event_cb_t on_click) {
    return kit::action_button(parent, text, on_click);
}
inline lv_obj_t *TsIconButton(lv_obj_t *parent, const char *symbol, lv_event_cb_t on_click, uint32_t tone = 0) {
    return kit::icon_button(parent, symbol, on_click, tone);
}
inline lv_obj_t *TsBattery(lv_obj_t *parent) { return kit::battery(parent); }
inline void TsSetBattery(lv_obj_t *widget, int percent) { kit::set_battery(widget, percent); }
inline lv_obj_t *TsWifi(lv_obj_t *parent) { return kit::wifi(parent); }
inline void TsSetWifi(lv_obj_t *widget, bool online, int rssi) { kit::set_wifi(widget, online, rssi); }
inline lv_obj_t *TsChip(lv_obj_t *parent, const char *text, bool selected, lv_event_cb_t on_click, void *user_data) {
    return kit::chip(parent, text, selected, on_click, user_data);
}
inline void TsSetChipSelected(lv_obj_t *chip, bool selected, bool strong = false) {
    kit::set_chip_selected(chip, selected, strong);
}
inline void TsSetChipTone(lv_obj_t *chip, uint32_t rgb) { kit::set_chip_tone(chip, rgb); }
inline void TsSetChipCount(lv_obj_t *chip, int count) { kit::set_chip_count(chip, count); }
inline lv_obj_t *TsSpinner(lv_obj_t *parent, int32_t size, int32_t dot) { return kit::spinner(parent, size, dot); }
inline kit::SubsysBar TsSubsysBar(lv_obj_t *parent, const char *back_symbol, lv_event_cb_t on_back, uint32_t tone,
                                  lv_event_cb_t on_action = nullptr, const char *action_symbol = nullptr) {
    return kit::subsys_bar(parent, back_symbol, on_back, tone, on_action, action_symbol);
}
inline lv_obj_t *TsCard(lv_obj_t *parent, const char *title, lv_event_cb_t on_click) {
    return kit::card(parent, title, on_click);
}
inline void TsSetCardDetail(lv_obj_t *card, const char *text) { kit::set_card_detail(card, text); }

}  // namespace ui
}  // namespace tessara
