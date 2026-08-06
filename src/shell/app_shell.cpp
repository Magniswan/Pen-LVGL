#include "shell/app_shell.h"

#include "shell/app_theme.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace dictpen {
namespace {

constexpr int32_t kGestureClaimDistance = 24;
constexpr int32_t kCloseClaimDistance = 10;

int battery_percent()
{
    const char* paths[] {
        "/sys/class/power_supply/battery/capacity",
        "/sys/class/power_supply/BAT0/capacity",
    };
    for(const char* path : paths) {
        std::ifstream stream(path);
        int value = -1;
        if(stream >> value && value >= 0 && value <= 100) return value;
    }
    return -1;
}

}  // namespace

AppShell::AppShell(AppControl& control, AppShellConfig config)
    : control_(control), config_(config)
{
}

AppShell::~AppShell()
{
    // RuntimeApplication::destroy releases LVGL-owned objects before lv_deinit.
}

void AppShell::create()
{
    lv_obj_t* layer = lv_layer_top();

    scrim_ = lv_obj_create(layer);
    lv_obj_set_pos(scrim_, 0, 0);
    lv_obj_set_size(scrim_, theme::screen_width, theme::screen_height);
    lv_obj_set_style_bg_color(scrim_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim_, LV_OPA_40, 0);
    lv_obj_set_style_border_width(scrim_, 0, 0);
    lv_obj_set_style_radius(scrim_, 0, 0);
    lv_obj_add_flag(scrim_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(scrim_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(scrim_, scrim_event, LV_EVENT_CLICKED, this);

    panel_ = lv_obj_create(layer);
    lv_obj_set_pos(panel_, 0, -theme::panel_height);
    lv_obj_set_size(panel_, theme::screen_width, theme::panel_height);
    lv_obj_set_style_bg_color(panel_, lv_color_hex(0x17212B), 0);
    lv_obj_set_style_border_width(panel_, 0, 0);
    lv_obj_set_style_radius(panel_, 0, 0);
    lv_obj_set_style_pad_left(panel_, 24, 0);
    lv_obj_set_style_pad_right(panel_, 18, 0);
    lv_obj_set_style_pad_top(panel_, 12, 0);
    lv_obj_set_style_pad_bottom(panel_, 10, 0);
    lv_obj_set_style_pad_gap(panel_, 12, 0);
    lv_obj_set_flex_flow(panel_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel_, gesture_event, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(panel_, gesture_event, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(panel_, gesture_event, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(panel_, gesture_event, LV_EVENT_PRESS_LOST, this);

    clock_label_ = theme::create_label(panel_, "--:--", lv_color_white());
    lv_obj_set_width(clock_label_, 78);
    lv_obj_set_style_text_font(clock_label_, &lv_font_montserrat_24, 0);

    lv_obj_t* app_label = theme::create_label(panel_, config_.title, lv_color_hex(0xD7E0E6));
    lv_obj_set_width(app_label, 330);
    lv_label_set_long_mode(app_label, LV_LABEL_LONG_MODE_DOTS);

    battery_label_ = theme::create_label(panel_, "电量 --", lv_color_hex(0xD7E0E6));
    lv_obj_set_width(battery_label_, 150);

    if(!config_.is_launcher) {
        lv_obj_t* home = theme::create_button(panel_, LV_SYMBOL_HOME, 64, theme::accent());
        lv_obj_set_style_text_font(lv_obj_get_child(home, 0), &lv_font_montserrat_24, 0);
        lv_obj_add_event_cb(home, home_event, LV_EVENT_CLICKED, this);
    }
    else {
        lv_obj_t* spacer = lv_obj_create(panel_);
        lv_obj_set_size(spacer, 64, theme::touch_target);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
    }

    lv_obj_t* exit = theme::create_button(panel_, LV_SYMBOL_POWER, 64, theme::danger());
    lv_obj_set_style_text_font(lv_obj_get_child(exit, 0), &lv_font_montserrat_24, 0);
    lv_obj_add_event_cb(exit, exit_event, LV_EVENT_CLICKED, this);

    gesture_ = lv_obj_create(layer);
    lv_obj_set_pos(gesture_, 0, 0);
    lv_obj_set_size(gesture_, theme::screen_width, theme::gesture_height);
    lv_obj_set_style_bg_opa(gesture_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gesture_, 0, 0);
    lv_obj_set_style_radius(gesture_, 0, 0);
    lv_obj_set_style_pad_all(gesture_, 0, 0);
    lv_obj_add_flag(gesture_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gesture_, gesture_event, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(gesture_, gesture_event, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(gesture_, gesture_event, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(gesture_, gesture_event, LV_EVENT_PRESS_LOST, this);

    timer_ = lv_timer_create(status_timer, 1000, this);
    update_status();
}

bool AppShell::stop_requested() const
{
    return stop_requested_;
}

void AppShell::destroy()
{
    if(timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if(panel_) lv_anim_delete(panel_, animate_panel_y);
    panel_ = nullptr;
    scrim_ = nullptr;
    gesture_ = nullptr;
    clock_label_ = nullptr;
    battery_label_ = nullptr;
}

void AppShell::begin_gesture()
{
    lv_indev_t* input = lv_indev_active();
    if(!input) return;
    lv_indev_get_point(input, &gesture_start_);
    panel_start_y_ = lv_obj_get_y(panel_);
}

void AppShell::update_gesture()
{
    lv_indev_t* input = lv_indev_active();
    if(!input) return;
    lv_point_t point {};
    lv_indev_get_point(input, &point);
    const int32_t delta_x = point.x - gesture_start_.x;
    const int32_t delta_y = point.y - gesture_start_.y;

    if(state_ == PanelState::closed) {
        if(delta_y < kGestureClaimDistance || delta_y <= std::abs(delta_x)) return;
        state_ = PanelState::dragging;
        lv_obj_remove_flag(scrim_, LV_OBJ_FLAG_HIDDEN);
    }
    else if(state_ == PanelState::open) {
        if(delta_y > -kCloseClaimDistance || -delta_y <= std::abs(delta_x)) return;
        state_ = PanelState::dragging;
    }
    if(state_ != PanelState::dragging) return;

    const int32_t y = std::clamp(panel_start_y_ + delta_y, -theme::panel_height, 0);
    lv_obj_set_y(panel_, y);
}

void AppShell::end_gesture()
{
    if(state_ != PanelState::dragging) return;
    settle(lv_obj_get_y(panel_) > -(theme::panel_height / 2));
}

void AppShell::settle(bool open)
{
    state_ = PanelState::settling;
    if(open) lv_obj_remove_flag(scrim_, LV_OBJ_FLAG_HIDDEN);

    lv_anim_delete(panel_, animate_panel_y);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, panel_);
    lv_anim_set_values(&animation, lv_obj_get_y(panel_), open ? 0 : -theme::panel_height);
    lv_anim_set_duration(&animation, 200);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, animate_panel_y);
    lv_anim_set_user_data(&animation, this);
    lv_anim_set_completed_cb(&animation, panel_animation_completed);
    lv_anim_start(&animation);
}

void AppShell::update_status()
{
    std::time_t now = std::time(nullptr);
    std::tm local {};
    localtime_r(&now, &local);
    char clock[16] {};
    std::strftime(clock, sizeof(clock), "%H:%M", &local);
    lv_label_set_text(clock_label_, clock);

    const int battery = battery_percent();
    if(battery < 0) lv_label_set_text(battery_label_, "电量 --");
    else lv_label_set_text_fmt(battery_label_, "电量 %d%%", battery);
}

void AppShell::request_home()
{
    control_.home();
    stop_requested_ = true;
}

void AppShell::request_exit()
{
    control_.exit_session();
    stop_requested_ = true;
}

void AppShell::gesture_event(lv_event_t* event)
{
    auto* self = static_cast<AppShell*>(lv_event_get_user_data(event));
    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_PRESSED) self->begin_gesture();
    else if(code == LV_EVENT_PRESSING) self->update_gesture();
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) self->end_gesture();
}

void AppShell::scrim_event(lv_event_t* event)
{
    static_cast<AppShell*>(lv_event_get_user_data(event))->settle(false);
}

void AppShell::home_event(lv_event_t* event)
{
    static_cast<AppShell*>(lv_event_get_user_data(event))->request_home();
}

void AppShell::exit_event(lv_event_t* event)
{
    auto* self = static_cast<AppShell*>(lv_event_get_user_data(event));
    lv_obj_t* box = lv_msgbox_create(lv_layer_top());
    lv_msgbox_add_title(box, "退出 LVGL？");
    lv_msgbox_add_text(box, "退出后将返回 Falcon。");
    lv_obj_t* cancel = lv_msgbox_add_footer_button(box, "取消");
    lv_obj_t* confirm = lv_msgbox_add_footer_button(box, "退出");
    lv_obj_add_event_cb(cancel, confirm_exit_event, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(confirm, confirm_exit_event, LV_EVENT_CLICKED, self);
    lv_obj_set_width(box, 420);
    lv_obj_set_style_text_font(box, theme::body_font(), 0);
}

void AppShell::confirm_exit_event(lv_event_t* event)
{
    auto* self = static_cast<AppShell*>(lv_event_get_user_data(event));
    lv_obj_t* button = lv_event_get_target_obj(event);
    lv_obj_t* box = lv_obj_get_parent(lv_obj_get_parent(button));
    if(self) self->request_exit();
    lv_msgbox_close(box);
}

void AppShell::status_timer(lv_timer_t* timer)
{
    static_cast<AppShell*>(lv_timer_get_user_data(timer))->update_status();
}

void AppShell::animate_panel_y(void* object, int32_t value)
{
    lv_obj_set_y(static_cast<lv_obj_t*>(object), value);
}

void AppShell::panel_animation_completed(lv_anim_t* animation)
{
    auto* self = static_cast<AppShell*>(lv_anim_get_user_data(animation));
    const bool open = lv_obj_get_y(self->panel_) == 0;
    self->state_ = open ? PanelState::open : PanelState::closed;
    if(!open) lv_obj_add_flag(self->scrim_, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace dictpen
