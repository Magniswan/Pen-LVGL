#pragma once

#include "runtime/app_control.h"

#include <lvgl.h>

namespace dictpen {

struct AppShellConfig {
    const char* title {"LVGL"};
    bool is_launcher {false};
};

class AppShell {
public:
    AppShell(AppControl& control, AppShellConfig config);
    ~AppShell();

    AppShell(const AppShell&) = delete;
    AppShell& operator=(const AppShell&) = delete;

    void create();
    void destroy();
    bool stop_requested() const;

private:
    enum class PanelState {
        closed,
        dragging,
        open,
        settling,
    };

    static void gesture_event(lv_event_t* event);
    static void scrim_event(lv_event_t* event);
    static void home_event(lv_event_t* event);
    static void exit_event(lv_event_t* event);
    static void confirm_exit_event(lv_event_t* event);
    static void status_timer(lv_timer_t* timer);
    static void animate_panel_y(void* object, int32_t value);
    static void panel_animation_completed(lv_anim_t* animation);

    void begin_gesture();
    void update_gesture();
    void end_gesture();
    void settle(bool open);
    void update_status();
    void request_home();
    void request_exit();

    AppControl& control_;
    AppShellConfig config_;
    PanelState state_ {PanelState::closed};
    lv_obj_t* scrim_ {nullptr};
    lv_obj_t* panel_ {nullptr};
    lv_obj_t* gesture_ {nullptr};
    lv_obj_t* clock_label_ {nullptr};
    lv_obj_t* battery_label_ {nullptr};
    lv_timer_t* timer_ {nullptr};
    lv_point_t gesture_start_ {};
    int32_t panel_start_y_ {0};
    bool stop_requested_ {false};
};

}  // namespace dictpen
