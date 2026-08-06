#pragma once

#include "focus_timer/focus_timer_model.h"

#include <lvgl.h>

namespace dictpen {

class FocusTimerUi {
public:
    void create();
    void destroy();

private:
    static void preset_event(lv_event_t* event);
    static void toggle_event(lv_event_t* event);
    static void reset_event(lv_event_t* event);
    static void refresh_timer(lv_timer_t* timer);

    void select_minutes(int minutes);
    void refresh();

    FocusTimerModel model_;
    lv_obj_t* arc_ {nullptr};
    lv_obj_t* time_label_ {nullptr};
    lv_obj_t* state_label_ {nullptr};
    lv_obj_t* toggle_label_ {nullptr};
    lv_obj_t* preset_buttons_[3] {};
    lv_timer_t* timer_ {nullptr};
};

}  // namespace dictpen
