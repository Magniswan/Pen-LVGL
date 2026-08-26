#include "installer/installer_ui.h"

#include "shell/app_theme.h"

#include <algorithm>
#include <utility>

namespace dictpen {
namespace {

void card(lv_obj_t* object)
{
    lv_obj_set_style_bg_color(object, theme::surface(), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, theme::line(), 0);
    lv_obj_set_style_radius(object, 8, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void set_button_text(lv_obj_t* label, const char* text)
{
    if(label != nullptr) lv_label_set_text(label, text);
}

}  // namespace

InstallerUi::InstallerUi(AppControl& control) : control_(control) {}

void InstallerUi::create()
{
    lv_obj_t* root = lv_screen_active();
    theme::style_screen(root);

    lv_obj_t* rail = lv_obj_create(root);
    lv_obj_set_pos(rail, 0, 0);
    lv_obj_set_size(rail, 228, theme::screen_height);
    lv_obj_set_style_bg_color(rail, lv_color_hex(0x17212B), 0);
    lv_obj_set_style_border_width(rail, 0, 0);
    lv_obj_set_style_radius(rail, 0, 0);
    lv_obj_set_style_pad_left(rail, 24, 0);
    lv_obj_set_style_pad_top(rail, 24, 0);
    lv_obj_remove_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
    auto* eyebrow = theme::create_label(rail, "TRUSTED LIFECYCLE", lv_color_hex(0x7ED5C8));
    lv_obj_set_style_text_font(eyebrow, &lv_font_montserrat_14, 0);
    auto* title = theme::create_label(rail, "应用管理", lv_color_white());
    lv_obj_set_pos(title, 0, 31);

    inbox_tab_ = theme::create_button(rail, "收件箱", 180, theme::accent());
    lv_obj_set_pos(inbox_tab_, 0, 76);
    lv_obj_add_event_cb(inbox_tab_, inbox_tab_event, LV_EVENT_CLICKED, this);
    installed_tab_ = theme::create_button(rail, "已安装", 180, lv_color_hex(0x364553));
    lv_obj_set_pos(installed_tab_, 0, 126);
    lv_obj_add_event_cb(installed_tab_, installed_tab_event, LV_EVENT_CLICKED, this);
    auto* seal = theme::create_label(rail, "● 官方密钥 · 无绕过", lv_color_hex(0x66C6B7));
    lv_obj_set_pos(seal, 0, 212);

    lv_obj_t* panel = lv_obj_create(root);
    lv_obj_set_pos(panel, 248, 22);
    lv_obj_set_size(panel, 690, 220);
    card(panel);
    lv_obj_set_style_pad_left(panel, 24, 0);
    lv_obj_set_style_pad_top(panel, 20, 0);

    identity_ = theme::create_label(panel, "正在读取安全收件箱…", theme::ink());
    lv_obj_set_width(identity_, 470);
    metadata_ = theme::create_label(panel, "", theme::muted());
    lv_obj_set_pos(metadata_, 0, 38);
    lv_obj_set_width(metadata_, 500);
    verification_ = theme::create_label(panel, "", theme::accent());
    lv_obj_set_pos(verification_, 0, 82);
    lv_obj_set_width(verification_, 500);
    lv_label_set_long_mode(verification_, LV_LABEL_LONG_MODE_DOTS);

    next_ = theme::create_button(panel, "下一个", 112, lv_color_hex(0x66727D));
    lv_obj_set_pos(next_, 526, 8);
    lv_obj_add_event_cb(next_, next_event, LV_EVENT_CLICKED, this);
    secondary_ = theme::create_button(panel, "回滚", 142, lv_color_hex(0xB96B3E));
    lv_obj_set_pos(secondary_, 316, 140);
    secondary_label_ = lv_obj_get_child(secondary_, 0);
    lv_obj_add_event_cb(secondary_, secondary_event, LV_EVENT_CLICKED, this);
    action_ = theme::create_button(panel, "验证并安装", 172, theme::accent());
    lv_obj_set_pos(action_, 466, 140);
    action_label_ = lv_obj_get_child(action_, 0);
    lv_obj_add_event_cb(action_, action_event, LV_EVENT_CLICKED, this);

    confirm_layer_ = lv_obj_create(root);
    lv_obj_set_pos(confirm_layer_, 0, 0);
    lv_obj_set_size(confirm_layer_, theme::screen_width, theme::screen_height);
    lv_obj_set_style_bg_color(confirm_layer_, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(confirm_layer_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(confirm_layer_, 0, 0);
    lv_obj_set_style_radius(confirm_layer_, 0, 0);
    lv_obj_set_style_pad_all(confirm_layer_, 0, 0);
    lv_obj_remove_flag(confirm_layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* confirmation = lv_obj_create(confirm_layer_);
    lv_obj_set_pos(confirmation, 248, 38);
    lv_obj_set_size(confirmation, 464, 190);
    card(confirmation);
    lv_obj_set_style_pad_all(confirmation, 20, 0);
    confirm_title_ = theme::create_label(confirmation, "确认操作", theme::ink());
    confirm_detail_ = theme::create_label(confirmation, "", theme::muted());
    lv_obj_set_pos(confirm_detail_, 0, 38);
    lv_obj_set_width(confirm_detail_, 420);
    auto* cancel = theme::create_button(confirmation, "取消", 150, lv_color_hex(0x66727D));
    lv_obj_set_pos(cancel, 92, 116);
    lv_obj_add_event_cb(cancel, cancel_event, LV_EVENT_CLICKED, this);
    auto* confirm = theme::create_button(confirmation, "确认执行", 164, theme::danger());
    lv_obj_set_pos(confirm, 252, 116);
    lv_obj_add_event_cb(confirm, confirm_event, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(confirm_layer_, LV_OBJ_FLAG_HIDDEN);

    if(!installer_.available()) {
        scan_.status = lvgl_platform::InboxStatus::io_error;
        scan_.detail = "INSTALLER_BROKER_UNAVAILABLE";
    } else {
        scan_current();
    }
    render();
}

void InstallerUi::destroy()
{
    if(operation_timer_ != nullptr) {
        lv_timer_delete(operation_timer_);
        operation_timer_ = nullptr;
    }
}

bool InstallerUi::stop_requested() const noexcept { return stop_requested_; }

void InstallerUi::scan_current()
{
    if(mode_ == Mode::inbox) {
        scan_ = installer_.scan();
        selected_ = std::min(selected_, scan_.candidates.empty() ? std::size_t {0}
                                                                 : scan_.candidates.size() - 1);
    } else {
        installed_scan_ = installer_.scan_installed();
        selected_ = std::min(selected_, installed_scan_.applications.empty() ? std::size_t {0}
                                                                              : installed_scan_.applications.size() - 1);
    }
}

void InstallerUi::switch_mode(Mode mode)
{
    if(busy_ || result_success_ || mode_ == mode) return;
    mode_ = mode;
    selected_ = 0;
    result_text_.clear();
    pending_ = Operation::none;
    scan_current();
    render();
}

void InstallerUi::render()
{
    lv_obj_set_style_bg_color(inbox_tab_, mode_ == Mode::inbox ? theme::accent()
                                                               : lv_color_hex(0x364553), 0);
    lv_obj_set_style_bg_color(installed_tab_, mode_ == Mode::installed ? theme::accent()
                                                                       : lv_color_hex(0x364553), 0);
    lv_obj_add_flag(secondary_, LV_OBJ_FLAG_HIDDEN);
    if(!result_text_.empty()) {
        lv_label_set_text(identity_, result_success_ ? "安全事务已完成" : "安全事务未提交");
        lv_label_set_text(metadata_, result_text_.c_str());
        lv_label_set_text(verification_, result_success_
                                             ? "✓ 注册表将在返回桌面时重新复验并刷新"
                                             : "没有绕过、强制执行或忽略校验的路径");
        lv_obj_set_style_text_color(verification_,
                                    result_success_ ? theme::accent() : theme::danger(), 0);
        lv_obj_add_flag(next_, LV_OBJ_FLAG_HIDDEN);
        set_button_text(action_label_, result_success_ ? "完成 · 返回桌面" : "返回检查");
        lv_obj_remove_state(action_, LV_STATE_DISABLED);
        return;
    }
    if(mode_ == Mode::inbox) {
        if(!scan_.ok() || scan_.candidates.empty()) {
            lv_label_set_text(identity_, scan_.status == lvgl_platform::InboxStatus::empty
                                           ? "安全收件箱为空"
                                           : "无法信任安全收件箱");
            lv_label_set_text(metadata_, scan_.detail.c_str());
            lv_label_set_text(verification_, "请放入官方签名 .lvapp 后重新检查");
            lv_obj_set_style_text_color(verification_, theme::danger(), 0);
            lv_obj_add_flag(next_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(action_, LV_STATE_DISABLED);
            set_button_text(action_label_, "无可安装项目");
            return;
        }
        const auto& candidate = scan_.candidates[selected_];
        lv_label_set_text(identity_, candidate.name.c_str());
        lv_label_set_text_fmt(metadata_, "%s  ·  v%s  ·  发布 %llu  ·  %llu KiB",
                              candidate.app_id.c_str(), candidate.version.c_str(),
                              static_cast<unsigned long long>(candidate.release_counter),
                              static_cast<unsigned long long>((candidate.package_size + 1023) / 1024));
        lv_label_set_text_fmt(verification_, "%s  %s",
                              candidate.installable ? "✓ 官方签名与兼容性通过" : "× 校验拒绝",
                              candidate.detail.c_str());
        lv_obj_set_style_text_color(verification_,
                                    candidate.installable ? theme::accent() : theme::danger(), 0);
        if(candidate.installable) lv_obj_remove_state(action_, LV_STATE_DISABLED);
        else lv_obj_add_state(action_, LV_STATE_DISABLED);
        set_button_text(action_label_, candidate.installable ? "验证并安装" : "不可安装");
        if(scan_.candidates.size() > 1) lv_obj_remove_flag(next_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(next_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if(!installed_scan_.success || installed_scan_.applications.empty()) {
        lv_label_set_text(identity_, installed_scan_.success ? "没有第三方应用" : "无法读取已安装应用");
        lv_label_set_text(metadata_, installed_scan_.detail.c_str());
        lv_label_set_text(verification_, installed_scan_.success
                                             ? "平台内置桌面、安装器与 2048 不在此处移除"
                                             : "生命周期 broker 已失败关闭");
        lv_obj_set_style_text_color(verification_,
                                    installed_scan_.success ? theme::muted() : theme::danger(), 0);
        lv_obj_add_flag(next_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(action_, LV_STATE_DISABLED);
        set_button_text(action_label_, "无可管理应用");
        return;
    }
    const auto& application = installed_scan_.applications[selected_];
    lv_label_set_text(identity_, application.name.c_str());
    if(application.policy_trusted) {
        lv_label_set_text_fmt(metadata_, "%s  ·  %s  ·  当前 %llu%s",
                              application.app_id.c_str(), application.version.c_str(),
                              static_cast<unsigned long long>(application.current_release),
                              application.previous_release == 0 ? "" : "  ·  有历史版本");
    } else {
        lv_label_set_text_fmt(metadata_, "%s  ·  反回滚状态不可用",
                              application.app_id.c_str());
    }
    lv_label_set_text_fmt(verification_, "%s  %s",
                          application.current_verified ? "✓ 当前版本已复验" : "× 当前 payload 不可信",
                          application.detail.c_str());
    lv_obj_set_style_text_color(verification_,
                                application.current_verified ? theme::accent() : theme::danger(), 0);
    lv_obj_remove_state(action_, LV_STATE_DISABLED);
    set_button_text(action_label_, "卸载 payload");
    lv_obj_remove_flag(secondary_, LV_OBJ_FLAG_HIDDEN);
    if(application.rollback_available) lv_obj_remove_state(secondary_, LV_STATE_DISABLED);
    else lv_obj_add_state(secondary_, LV_STATE_DISABLED);
    set_button_text(secondary_label_, application.rollback_available ? "回滚并隔离" : "无可信回滚");
    if(installed_scan_.applications.size() > 1) lv_obj_remove_flag(next_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(next_, LV_OBJ_FLAG_HIDDEN);
}

void InstallerUi::show_confirmation(Operation operation)
{
    pending_ = operation;
    if(operation == Operation::rollback) {
        lv_label_set_text(confirm_title_, "回滚到可信历史版本？");
        lv_label_set_text(confirm_detail_,
                          "root broker 会重新验签 previous，并把当前版本隔离。高水位不会下降。");
    } else {
        lv_label_set_text(confirm_title_, "仅卸载应用 payload？");
        lv_label_set_text(confirm_detail_,
                          "可执行文件将被有界删除；反回滚策略与应用私有数据会保留，便于安全重装。");
    }
    lv_obj_remove_flag(confirm_layer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(confirm_layer_);
}

void InstallerUi::begin_operation(Operation operation)
{
    if(busy_) return;
    if(operation == Operation::install) {
        if(selected_ >= scan_.candidates.size() || !scan_.candidates[selected_].installable) return;
        pending_ = operation;
    } else {
        if(selected_ >= installed_scan_.applications.size()) return;
        if(operation == Operation::rollback &&
           !installed_scan_.applications[selected_].rollback_available) return;
        show_confirmation(operation);
        return;
    }
    busy_ = true;
    lv_obj_add_state(action_, LV_STATE_DISABLED);
    lv_obj_add_state(secondary_, LV_STATE_DISABLED);
    set_button_text(action_label_, "正在执行安全事务…");
    lv_label_set_text(verification_, "root broker 正在重新枚举、复验并原子提交");
    operation_timer_ = lv_timer_create(operation_timer, 80, this);
    lv_timer_set_repeat_count(operation_timer_, 1);
}

void InstallerUi::finish_operation(bool success, std::string detail)
{
    result_success_ = success;
    result_text_ = std::move(detail);
    busy_ = false;
    pending_ = Operation::none;
    render();
}

void InstallerUi::execute_operation()
{
    operation_timer_ = nullptr;
    InstallerClientResult result;
    if(pending_ == Operation::install) {
        result = installer_.install(scan_.candidates[selected_].token);
    } else if(pending_ == Operation::rollback) {
        result = installer_.rollback(installed_scan_.applications[selected_].token);
    } else if(pending_ == Operation::remove) {
        result = installer_.remove(installed_scan_.applications[selected_].token);
    } else {
        finish_operation(false, "INSTALLER_OPERATION_INVALID");
        return;
    }
    finish_operation(result.success, std::move(result.detail));
}

void InstallerUi::action_event(lv_event_t* event)
{
    auto* self = static_cast<InstallerUi*>(lv_event_get_user_data(event));
    if(!self->result_text_.empty()) {
        if(self->result_success_) {
            self->control_.home();
            self->stop_requested_ = true;
        } else {
            self->result_text_.clear();
            self->scan_current();
            self->render();
        }
        return;
    }
    self->begin_operation(self->mode_ == Mode::inbox ? Operation::install : Operation::remove);
}

void InstallerUi::secondary_event(lv_event_t* event)
{
    static_cast<InstallerUi*>(lv_event_get_user_data(event))->begin_operation(Operation::rollback);
}

void InstallerUi::next_event(lv_event_t* event)
{
    auto* self = static_cast<InstallerUi*>(lv_event_get_user_data(event));
    if(self->busy_) return;
    const auto count = self->mode_ == Mode::inbox ? self->scan_.candidates.size()
                                                  : self->installed_scan_.applications.size();
    if(count == 0) return;
    self->selected_ = (self->selected_ + 1) % count;
    self->render();
}

void InstallerUi::inbox_tab_event(lv_event_t* event)
{
    static_cast<InstallerUi*>(lv_event_get_user_data(event))->switch_mode(Mode::inbox);
}

void InstallerUi::installed_tab_event(lv_event_t* event)
{
    static_cast<InstallerUi*>(lv_event_get_user_data(event))->switch_mode(Mode::installed);
}

void InstallerUi::confirm_event(lv_event_t* event)
{
    auto* self = static_cast<InstallerUi*>(lv_event_get_user_data(event));
    lv_obj_add_flag(self->confirm_layer_, LV_OBJ_FLAG_HIDDEN);
    self->busy_ = true;
    lv_obj_add_state(self->action_, LV_STATE_DISABLED);
    lv_obj_add_state(self->secondary_, LV_STATE_DISABLED);
    set_button_text(self->action_label_, "正在执行安全事务…");
    lv_label_set_text(self->verification_, "正在复核快照令牌、可信状态与固定目录边界");
    self->operation_timer_ = lv_timer_create(operation_timer, 80, self);
    lv_timer_set_repeat_count(self->operation_timer_, 1);
}

void InstallerUi::cancel_event(lv_event_t* event)
{
    auto* self = static_cast<InstallerUi*>(lv_event_get_user_data(event));
    self->pending_ = Operation::none;
    lv_obj_add_flag(self->confirm_layer_, LV_OBJ_FLAG_HIDDEN);
}

void InstallerUi::operation_timer(lv_timer_t* timer)
{
    static_cast<InstallerUi*>(lv_timer_get_user_data(timer))->execute_operation();
}

}  // namespace dictpen
