#pragma once

#include "lvgl_platform/inbox_service.h"
#include "runtime/app_control.h"
#include "runtime/installer_client.h"

#include <lvgl.h>

#include <string>

namespace dictpen {

class InstallerUi {
public:
    explicit InstallerUi(AppControl& control);

    void create();
    void destroy();
    bool stop_requested() const noexcept;

private:
    static void action_event(lv_event_t* event);
    static void next_event(lv_event_t* event);
    static void install_timer(lv_timer_t* timer);

    void scan();
    void render();
    void begin_install();
    void install_selected();
    AppControl& control_;
    InstallerClient installer_;
    lvgl_platform::InboxScanResult scan_;
    std::size_t selected_ {0};
    lv_obj_t* identity_ {nullptr};
    lv_obj_t* metadata_ {nullptr};
    lv_obj_t* verification_ {nullptr};
    lv_obj_t* action_ {nullptr};
    lv_obj_t* action_label_ {nullptr};
    lv_obj_t* next_ {nullptr};
    lv_timer_t* install_timer_ {nullptr};
    std::string result_text_;
    bool result_success_ {false};
    bool busy_ {false};
    bool stop_requested_ {false};
};

}  // namespace dictpen
