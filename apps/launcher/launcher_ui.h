#pragma once

#include "runtime/app_control.h"

#include <lvgl.h>

#include <string_view>

namespace dictpen {

class LauncherUi {
public:
    explicit LauncherUi(AppControl& control);

    void create();
    bool stop_requested() const;

private:
    static void app_event(lv_event_t* event);
    static void pager_event(lv_event_t* event);

    void launch(std::string_view app_id, const char* display_name);
    void update_page_indicator();

    AppControl& control_;
    lv_obj_t* pager_ {nullptr};
    lv_obj_t* page_label_ {nullptr};
    lv_obj_t* transition_label_ {nullptr};
    unsigned page_count_ {0};
    bool stop_requested_ {false};
};

}  // namespace dictpen
