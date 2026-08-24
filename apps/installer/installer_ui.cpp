#include "installer/installer_ui.h"

#include "lvgl_platform/version.h"
#include "shell/app_theme.h"

#include <algorithm>
#include <cstdlib>

namespace dictpen {
namespace {

const char* environment(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : fallback;
}

void card(lv_obj_t* object)
{
    lv_obj_set_style_bg_color(object, theme::surface(), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, theme::line(), 0);
    lv_obj_set_style_radius(object, 8, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
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
    lv_obj_set_style_pad_top(rail, 32, 0);
    lv_obj_remove_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
    auto* eyebrow = theme::create_label(rail, "OFFICIAL INBOX", lv_color_hex(0x7ED5C8));
    lv_obj_set_style_text_font(eyebrow, &lv_font_montserrat_14, 0);
    auto* title = theme::create_label(rail, "应用安装器", lv_color_white());
    lv_obj_set_pos(title, 0, 34);
    auto* stages = theme::create_label(
        rail, "01  检查\n02  验签\n03  安装\n04  结果", lv_color_hex(0xAEBBC7));
    lv_obj_set_pos(stages, 0, 82);
    lv_obj_set_style_text_line_space(stages, 11, 0);
    auto* seal = theme::create_label(rail, "● 仅官方发布密钥", lv_color_hex(0x66C6B7));
    lv_obj_set_pos(seal, 0, 210);

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
    action_ = theme::create_button(panel, "安装", 172, theme::accent());
    lv_obj_set_pos(action_, 466, 140);
    action_label_ = lv_obj_get_child(action_, 0);
    lv_obj_add_event_cb(action_, action_event, LV_EVENT_CLICKED, this);

    crypto_ = lvgl_platform::CryptoProvider::load_default();
    if(crypto_ == nullptr) {
        scan_.status = lvgl_platform::InboxStatus::io_error;
        scan_.detail = "CRYPTO_UNAVAILABLE";
    } else if(lvgl_platform::prepare_application_storage() != lvgl_platform::InboxStatus::ready) {
        scan_.status = lvgl_platform::InboxStatus::root_untrusted;
        scan_.detail = "INBOX_ROOT_UNTRUSTED";
    } else {
        scan();
    }
    render();
}

void InstallerUi::destroy()
{
    if(install_timer_ != nullptr) {
        lv_timer_delete(install_timer_);
        install_timer_ = nullptr;
    }
}

bool InstallerUi::stop_requested() const noexcept { return stop_requested_; }

lvgl_platform::InstallPolicyContext InstallerUi::policy() const
{
    return {lvgl_platform::kPlatformVersion, lvgl_platform::kSdkAbi,
            environment("LVGL_PROFILE_ID", "invalid-profile"),
            environment("LVGL_MACHINE", "invalid-machine"), {"storage.private"}};
}

void InstallerUi::scan()
{
    scan_ = lvgl_platform::scan_official_inbox(policy(), *crypto_);
    selected_ = std::min(selected_, scan_.candidates.empty() ? std::size_t {0}
                                                             : scan_.candidates.size() - 1);
}

void InstallerUi::render()
{
    if(!result_text_.empty()) {
        lv_label_set_text(identity_, result_success_ ? "安装事务已完成" : "安装被安全策略拒绝");
        lv_label_set_text(metadata_, result_text_.c_str());
        lv_label_set_text(verification_, result_success_ ? "✓ 注册表将在返回桌面时重新复验并刷新"
                                                         : "未写入可启动注册表；没有“仍然安装”路径");
        lv_obj_add_flag(next_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(action_label_, result_success_ ? "完成 · 返回桌面" : "返回检查");
        lv_obj_remove_state(action_, LV_STATE_DISABLED);
        return;
    }
    if(!scan_.ok() || scan_.candidates.empty()) {
        lv_label_set_text(identity_, scan_.status == lvgl_platform::InboxStatus::empty
                                       ? "安全收件箱为空"
                                       : "无法信任安全收件箱");
        lv_label_set_text(metadata_, scan_.detail.c_str());
        lv_label_set_text(verification_, "请复制官方签名 .lvapp 后重新进入");
        lv_obj_add_flag(next_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(action_, LV_STATE_DISABLED);
        lv_label_set_text(action_label_, "无可安装项目");
        return;
    }
    const auto& candidate = scan_.candidates[selected_];
    lv_label_set_text_fmt(identity_, "%s", candidate.name.empty() ? "无法识别的软件包" : candidate.name.c_str());
    lv_label_set_text_fmt(metadata_, "%s  ·  v%s  ·  发布序号 %llu  ·  %llu KiB",
                          candidate.app_id.empty() ? "身份不可用" : candidate.app_id.c_str(),
                          candidate.version.empty() ? "—" : candidate.version.c_str(),
                          static_cast<unsigned long long>(candidate.release_counter),
                          static_cast<unsigned long long>((candidate.package_size + 1023) / 1024));
    lv_label_set_text_fmt(verification_, "%s  %s",
                          candidate.installable ? "✓ 官方签名与兼容性通过" : "× 校验拒绝",
                          candidate.detail.c_str());
    lv_obj_set_style_text_color(verification_,
                                candidate.installable ? theme::accent() : theme::danger(), 0);
    if(scan_.candidates.size() > 1) lv_obj_remove_flag(next_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(next_, LV_OBJ_FLAG_HIDDEN);
    if(candidate.installable) lv_obj_remove_state(action_, LV_STATE_DISABLED);
    else lv_obj_add_state(action_, LV_STATE_DISABLED);
    lv_label_set_text(action_label_, candidate.installable ? "验证并安装" : "不可安装");
}

void InstallerUi::begin_install()
{
    if(busy_ || selected_ >= scan_.candidates.size() || !scan_.candidates[selected_].installable) return;
    busy_ = true;
    lv_obj_add_state(action_, LV_STATE_DISABLED);
    lv_label_set_text(action_label_, "正在执行原子安装…");
    lv_label_set_text(verification_, "正在重新读取、验签、检查反回滚并提交双槽状态");
    install_timer_ = lv_timer_create(install_timer, 80, this);
    lv_timer_set_repeat_count(install_timer_, 1);
}

void InstallerUi::install_selected()
{
    install_timer_ = nullptr;
    const auto token = scan_.candidates[selected_].token;
    const auto result = lvgl_platform::install_official_inbox_candidate(token, policy(), *crypto_);
    result_success_ = result.ok();
    result_text_ = result.detail;
    busy_ = false;
    render();
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
            self->scan();
            self->render();
        }
        return;
    }
    self->begin_install();
}

void InstallerUi::next_event(lv_event_t* event)
{
    auto* self = static_cast<InstallerUi*>(lv_event_get_user_data(event));
    if(self->busy_ || self->scan_.candidates.empty()) return;
    self->selected_ = (self->selected_ + 1) % self->scan_.candidates.size();
    self->render();
}

void InstallerUi::install_timer(lv_timer_t* timer)
{
    static_cast<InstallerUi*>(lv_timer_get_user_data(timer))->install_selected();
}

}  // namespace dictpen
