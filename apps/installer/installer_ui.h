#pragma once

#include "lvgl_platform/inbox_service.h"
#include "runtime/app_control.h"
#include "runtime/installer_client.h"

#include <lvgl.h>

#include <cstdint>
#include <string>

namespace dictpen {

class InstallerUi {
public:
    explicit InstallerUi(AppControl& control);

    void create();
    void destroy();
    bool stop_requested() const noexcept;

private:
    enum class Mode : std::uint8_t { inbox, installed };
    enum class Operation : std::uint8_t { none, install, rollback, remove };

    static void action_event(lv_event_t* event);
    static void secondary_event(lv_event_t* event);
    static void next_event(lv_event_t* event);
    static void inbox_tab_event(lv_event_t* event);
    static void installed_tab_event(lv_event_t* event);
    static void confirm_event(lv_event_t* event);
    static void cancel_event(lv_event_t* event);
    static void operation_timer(lv_timer_t* timer);

    void scan_current();
    void render();
    void switch_mode(Mode mode);
    void begin_operation(Operation operation);
    void show_confirmation(Operation operation);
    void execute_operation();
    void finish_operation(bool success, std::string detail);
    AppControl& control_;
    InstallerClient installer_;
    lvgl_platform::InboxScanResult scan_;
    InstalledApplicationScanResult installed_scan_;
    Mode mode_ {Mode::inbox};
    Operation pending_ {Operation::none};
    std::size_t selected_ {0};
    lv_obj_t* identity_ {nullptr};
    lv_obj_t* metadata_ {nullptr};
    lv_obj_t* verification_ {nullptr};
    lv_obj_t* action_ {nullptr};
    lv_obj_t* action_label_ {nullptr};
    lv_obj_t* secondary_ {nullptr};
    lv_obj_t* secondary_label_ {nullptr};
    lv_obj_t* next_ {nullptr};
    lv_obj_t* inbox_tab_ {nullptr};
    lv_obj_t* installed_tab_ {nullptr};
    lv_obj_t* confirm_layer_ {nullptr};
    lv_obj_t* confirm_title_ {nullptr};
    lv_obj_t* confirm_detail_ {nullptr};
    lv_timer_t* operation_timer_ {nullptr};
    std::string result_text_;
    bool result_success_ {false};
    bool busy_ {false};
    bool stop_requested_ {false};
};

}  // namespace dictpen
