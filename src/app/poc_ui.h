#pragma once

#include "diagnostics/runtime_metrics.h"

#include <lvgl.h>

namespace dictpen {

class PocUi {
public:
    explicit PocUi(RuntimeMetrics& metrics);

    void create();
    bool exit_requested() const;

private:
    static void nav_event(lv_event_t* event);
    static void exit_event(lv_event_t* event);
    static void confirm_exit_event(lv_event_t* event);
    static void interaction_event(lv_event_t* event);
    static void slider_event(lv_event_t* event);
    static void drag_event(lv_event_t* event);
    static void status_timer(lv_timer_t* timer);
    static void animate_x(void* object, int32_t value);

    lv_obj_t* button(lv_obj_t* parent, const char* text, int32_t width);
    lv_obj_t* panel(lv_obj_t* parent, int32_t width);
    lv_obj_t* label(lv_obj_t* parent, const char* text);
    void create_top_bar(lv_obj_t* root);
    void create_interaction_page(lv_obj_t* parent);
    void create_visual_page(lv_obj_t* parent);
    void create_diagnostics_page(lv_obj_t* parent);
    void show_page(unsigned index);

    RuntimeMetrics& metrics_;
    lv_obj_t* pages_[3] {};
    lv_obj_t* nav_buttons_[3] {};
    lv_obj_t* status_label_ {nullptr};
    lv_obj_t* diagnostics_label_ {nullptr};
    lv_obj_t* interaction_label_ {nullptr};
    lv_obj_t* slider_label_ {nullptr};
    bool exit_requested_ {false};
    unsigned tap_count_ {0};
};

}  // namespace dictpen
