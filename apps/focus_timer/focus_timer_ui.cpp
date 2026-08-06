#include "focus_timer/focus_timer_ui.h"

#include "shell/app_theme.h"

#include <cstdio>

namespace dictpen {

void FocusTimerUi::create()
{
    lv_obj_t* root = lv_screen_active();
    theme::style_screen(root);

    lv_obj_t* left = lv_obj_create(root);
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_size(left, 500, theme::screen_height);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x17212B), 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    arc_ = lv_arc_create(left);
    lv_obj_set_size(arc_, 206, 206);
    lv_obj_set_pos(arc_, 54, 30);
    lv_arc_set_range(arc_, 0, 1000);
    lv_arc_set_rotation(arc_, 270);
    lv_arc_set_bg_angles(arc_, 0, 360);
    lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc_, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_, lv_color_hex(0x34414D), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_, lv_color_hex(0x50C7B6), LV_PART_INDICATOR);
    lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);

    time_label_ = theme::create_label(left, "25:00", lv_color_white());
    lv_obj_set_pos(time_label_, 88, 105);
    lv_obj_set_width(time_label_, 140);
    lv_obj_set_style_text_font(time_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_transform_scale_x(time_label_, 360, 0);
    lv_obj_set_style_transform_scale_y(time_label_, 360, 0);
    lv_obj_set_style_text_align(time_label_, LV_TEXT_ALIGN_CENTER, 0);

    state_label_ = theme::create_label(left, "准备专注", lv_color_hex(0xAEBBC7));
    lv_obj_set_pos(state_label_, 290, 92);
    lv_obj_set_width(state_label_, 180);
    lv_obj_t* hint = theme::create_label(left, "下拉可打开状态栏", lv_color_hex(0x71808D));
    lv_obj_set_pos(hint, 290, 132);

    lv_obj_t* controls = lv_obj_create(root);
    lv_obj_set_pos(controls, 500, 0);
    lv_obj_set_size(controls, 460, theme::screen_height);
    lv_obj_set_style_bg_color(controls, theme::canvas(), 0);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_radius(controls, 0, 0);
    lv_obj_set_style_pad_left(controls, 32, 0);
    lv_obj_set_style_pad_top(controls, 38, 0);
    lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    theme::create_label(controls, "选择时长", theme::muted());
    const int presets[] {15, 25, 45};
    for(unsigned index = 0; index < 3; ++index) {
        char text[16] {};
        std::snprintf(text, sizeof(text), "%d", presets[index]);
        preset_buttons_[index] = theme::create_button(controls, text, 88, theme::surface());
        lv_obj_set_pos(preset_buttons_[index], static_cast<int32_t>(index) * 102, 34);
        lv_obj_set_style_text_color(lv_obj_get_child(preset_buttons_[index], 0), theme::ink(), 0);
        lv_obj_set_style_border_width(preset_buttons_[index], 1, 0);
        lv_obj_set_style_border_color(preset_buttons_[index], theme::line(), 0);
        lv_obj_set_user_data(preset_buttons_[index], reinterpret_cast<void*>(
            static_cast<intptr_t>(presets[index])));
        lv_obj_add_event_cb(preset_buttons_[index], preset_event, LV_EVENT_CLICKED, this);
    }

    lv_obj_t* toggle = theme::create_button(controls, LV_SYMBOL_PLAY, 132, theme::accent());
    lv_obj_set_pos(toggle, 0, 112);
    toggle_label_ = lv_obj_get_child(toggle, 0);
    lv_obj_set_style_text_font(toggle_label_, &lv_font_montserrat_24, 0);
    lv_obj_add_event_cb(toggle, toggle_event, LV_EVENT_CLICKED, this);

    lv_obj_t* reset = theme::create_button(controls, LV_SYMBOL_REFRESH, 132, lv_color_hex(0x53616D));
    lv_obj_set_pos(reset, 150, 112);
    lv_obj_set_style_text_font(lv_obj_get_child(reset, 0), &lv_font_montserrat_24, 0);
    lv_obj_add_event_cb(reset, reset_event, LV_EVENT_CLICKED, this);

    timer_ = lv_timer_create(refresh_timer, 100, this);
    refresh();
}

void FocusTimerUi::destroy()
{
    if(timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void FocusTimerUi::select_minutes(int minutes)
{
    model_.select_minutes(minutes);
    refresh();
}

void FocusTimerUi::refresh()
{
    model_.update();
    const int remaining = model_.remaining_seconds();
    lv_label_set_text_fmt(time_label_, "%02d:%02d", remaining / 60, remaining % 60);
    lv_arc_set_value(arc_, model_.progress_per_mille());

    const char* state_text = "准备专注";
    const char* toggle_symbol = LV_SYMBOL_PLAY;
    if(model_.state() == FocusTimerModel::State::running) {
        state_text = "专注进行中";
        toggle_symbol = LV_SYMBOL_PAUSE;
    }
    else if(model_.state() == FocusTimerModel::State::paused) state_text = "已暂停";
    else if(model_.state() == FocusTimerModel::State::completed) state_text = "本轮完成";
    lv_label_set_text(state_label_, state_text);
    lv_label_set_text(toggle_label_, toggle_symbol);

    const int presets[] {15, 25, 45};
    for(unsigned index = 0; index < 3; ++index) {
        const bool selected = presets[index] == model_.selected_minutes();
        lv_obj_set_style_bg_color(preset_buttons_[index],
                                  selected ? lv_color_hex(0xD8F0EB) : theme::surface(), 0);
        if(model_.state() == FocusTimerModel::State::running)
            lv_obj_add_state(preset_buttons_[index], LV_STATE_DISABLED);
        else
            lv_obj_remove_state(preset_buttons_[index], LV_STATE_DISABLED);
    }
}

void FocusTimerUi::preset_event(lv_event_t* event)
{
    auto* self = static_cast<FocusTimerUi*>(lv_event_get_user_data(event));
    const auto value = reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    self->select_minutes(static_cast<int>(value));
}

void FocusTimerUi::toggle_event(lv_event_t* event)
{
    auto* self = static_cast<FocusTimerUi*>(lv_event_get_user_data(event));
    self->model_.toggle();
    self->refresh();
}

void FocusTimerUi::reset_event(lv_event_t* event)
{
    auto* self = static_cast<FocusTimerUi*>(lv_event_get_user_data(event));
    self->model_.reset();
    self->refresh();
}

void FocusTimerUi::refresh_timer(lv_timer_t* timer)
{
    static_cast<FocusTimerUi*>(lv_timer_get_user_data(timer))->refresh();
}

}  // namespace dictpen
