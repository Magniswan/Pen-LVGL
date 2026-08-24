#include "launcher/launcher_ui.h"

#include "session/app_registry.h"
#include "shell/app_theme.h"

#include <algorithm>
#include <cstdint>

namespace dictpen {
namespace {

constexpr int32_t kPagerWidth = 676;
constexpr int32_t kPageHeight = 178;
constexpr unsigned kAppsPerPage = 2;

}  // namespace

LauncherUi::LauncherUi(AppControl& control)
    : control_(control)
{
}

void LauncherUi::create()
{
    lv_obj_t* root = lv_screen_active();
    theme::style_screen(root);

    lv_obj_t* brand = lv_obj_create(root);
    lv_obj_set_pos(brand, 0, 0);
    lv_obj_set_size(brand, 250, theme::screen_height);
    lv_obj_set_style_bg_color(brand, lv_color_hex(0x17212B), 0);
    lv_obj_set_style_border_width(brand, 0, 0);
    lv_obj_set_style_radius(brand, 0, 0);
    lv_obj_set_style_pad_left(brand, 28, 0);
    lv_obj_set_style_pad_top(brand, 58, 0);
    lv_obj_remove_flag(brand, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* eyebrow = theme::create_label(brand, "LVGL APPS", lv_color_hex(0x7ED5C8));
    lv_obj_set_style_text_font(eyebrow, &lv_font_montserrat_16, 0);
    lv_obj_t* title = theme::create_label(brand, "应用中心", lv_color_white());
    lv_obj_set_pos(title, 0, 34);
    lv_obj_set_style_text_font(title, theme::body_font(), 0);
    lv_obj_set_style_transform_scale_x(title, 320, 0);
    lv_obj_set_style_transform_scale_y(title, 320, 0);
    lv_obj_t* detail = theme::create_label(brand, "左右滑动选择应用", lv_color_hex(0xAEBBC7));
    lv_obj_set_pos(detail, 0, 92);

    pager_ = lv_obj_create(root);
    lv_obj_set_pos(pager_, 266, 28);
    lv_obj_set_size(pager_, kPagerWidth, kPageHeight);
    lv_obj_set_style_bg_opa(pager_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pager_, 0, 0);
    lv_obj_set_style_radius(pager_, 0, 0);
    lv_obj_set_style_pad_all(pager_, 0, 0);
    lv_obj_set_style_pad_gap(pager_, 0, 0);
    lv_obj_set_flex_flow(pager_, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(pager_, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(pager_, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(pager_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(pager_, pager_event, LV_EVENT_SCROLL_END, this);

    std::size_t registry_count = 0;
    const AppDescriptor* registry = app_registry(registry_count);
    unsigned app_count = 0;
    for(std::size_t index = 0; index < registry_count; ++index) {
        if(registry[index].id != AppId::launcher) ++app_count;
    }
    page_count_ = std::max(1U, (app_count + kAppsPerPage - 1U) / kAppsPerPage);

    unsigned visible_index = 0;
    lv_obj_t* page = nullptr;
    for(std::size_t index = 0; index < registry_count; ++index) {
        const AppDescriptor& app = registry[index];
        if(app.id == AppId::launcher) continue;
        if(visible_index % kAppsPerPage == 0) {
            page = lv_obj_create(pager_);
            lv_obj_set_size(page, kPagerWidth, kPageHeight);
            lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(page, 0, 0);
            lv_obj_set_style_radius(page, 0, 0);
            lv_obj_set_style_pad_all(page, 4, 0);
            lv_obj_set_style_pad_gap(page, 14, 0);
            lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW);
            lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(page, LV_OBJ_FLAG_SNAPPABLE);
        }

        lv_obj_t* entry = lv_button_create(page);
        lv_obj_set_size(entry, 320, 164);
        theme::style_button(entry, theme::surface());
        lv_obj_set_style_bg_color(entry, lv_color_hex(0xE8F4F1), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(entry, 1, 0);
        lv_obj_set_style_border_color(entry, theme::line(), 0);
        lv_obj_set_style_pad_left(entry, 20, 0);
        lv_obj_set_style_pad_top(entry, 18, 0);
        lv_obj_set_user_data(entry, const_cast<AppDescriptor*>(&app));
        lv_obj_add_event_cb(entry, app_event, LV_EVENT_CLICKED, this);

        lv_obj_t* icon = theme::create_label(entry, app.symbol, theme::accent());
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        lv_obj_t* name = theme::create_label(entry, app.display_name, theme::ink());
        lv_obj_set_pos(name, 0, 54);
        lv_obj_set_width(name, 280);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_t* summary = theme::create_label(entry, app.summary, theme::muted());
        lv_obj_set_pos(summary, 0, 92);
        lv_obj_set_width(summary, 280);
        lv_label_set_long_mode(summary, LV_LABEL_LONG_MODE_DOTS);
        ++visible_index;
    }

    page_label_ = theme::create_label(root, "1 / 1", theme::muted());
    lv_obj_set_pos(page_label_, 870, 225);
    lv_obj_set_width(page_label_, 64);
    lv_obj_set_style_text_align(page_label_, LV_TEXT_ALIGN_RIGHT, 0);

    transition_label_ = theme::create_label(root, "", theme::accent());
    lv_obj_set_pos(transition_label_, 280, 222);
    lv_obj_set_width(transition_label_, 500);
    if(const char* error = std::getenv("LVGL_APP_ERROR")) {
        lv_label_set_text_fmt(transition_label_, "上个应用异常退出：%s", error);
    }
    update_page_indicator();
}

bool LauncherUi::stop_requested() const
{
    return stop_requested_;
}

void LauncherUi::launch(std::string_view app_id, const char* display_name)
{
    if(app_id.empty() || display_name == nullptr) return;
    lv_label_set_text_fmt(transition_label_, "正在打开 %s...", display_name);
    lv_obj_add_state(pager_, LV_STATE_DISABLED);
    if(control_.launch(app_id)) stop_requested_ = true;
    else {
        lv_obj_remove_state(pager_, LV_STATE_DISABLED);
        lv_label_set_text(transition_label_, "无法连接应用会话");
    }
}

void LauncherUi::update_page_indicator()
{
    const int32_t scroll_x = lv_obj_get_scroll_x(pager_);
    const unsigned page = std::min(page_count_,
        static_cast<unsigned>((std::max(0, scroll_x) + kPagerWidth / 2) / kPagerWidth + 1));
    lv_label_set_text_fmt(page_label_, "%u / %u", page, page_count_);
}

void LauncherUi::app_event(lv_event_t* event)
{
    auto* self = static_cast<LauncherUi*>(lv_event_get_user_data(event));
    auto* app = static_cast<AppDescriptor*>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    if(app) self->launch(app->stable_id, app->display_name);
}

void LauncherUi::pager_event(lv_event_t* event)
{
    static_cast<LauncherUi*>(lv_event_get_user_data(event))->update_page_indicator();
}

}  // namespace dictpen
