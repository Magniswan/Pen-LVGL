#include "app/poc_ui.h"

#include "shell/app_theme.h"

#include <algorithm>
#include <cstdio>

namespace dictpen {

PocUi::PocUi(RuntimeMetrics& metrics)
    : metrics_(metrics)
{
}

lv_obj_t* PocUi::panel(lv_obj_t* parent, int32_t width)
{
    lv_obj_t* value = lv_obj_create(parent);
    lv_obj_set_size(value, width, 188);
    lv_obj_set_style_radius(value, 6, 0);
    lv_obj_set_style_bg_color(value, theme::surface(), 0);
    lv_obj_set_style_border_color(value, theme::line(), 0);
    lv_obj_set_style_border_width(value, 1, 0);
    lv_obj_set_style_pad_all(value, 10, 0);
    lv_obj_set_style_pad_gap(value, 7, 0);
    lv_obj_set_flex_flow(value, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(value, LV_OBJ_FLAG_SCROLLABLE);
    return value;
}

void PocUi::create()
{
    lv_obj_t* root = lv_screen_active();
    theme::style_screen(root);
    create_navigation(root);

    lv_obj_t* content = lv_obj_create(root);
    lv_obj_set_pos(content, 0, 70);
    lv_obj_set_size(content, theme::screen_width, 196);
    lv_obj_set_style_bg_color(content, theme::canvas(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 4, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    for(auto& page : pages_) {
        page = lv_obj_create(content);
        lv_obj_set_pos(page, 0, 0);
        lv_obj_set_size(page, 952, 188);
        lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 0, 0);
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    }

    create_interaction_page(pages_[0]);
    create_visual_page(pages_[1]);
    create_diagnostics_page(pages_[2]);
    show_page(0);
    timer_ = lv_timer_create(status_timer, 500, this);
}

void PocUi::destroy()
{
    if(timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void PocUi::create_navigation(lv_obj_t* root)
{
    lv_obj_t* bar = lv_obj_create(root);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, theme::screen_width, 70);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x17212B), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_left(bar, 18, 0);
    lv_obj_set_style_pad_top(bar, 20, 0);
    lv_obj_set_style_pad_gap(bar, 8, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    const char* names[] {"交互", "视觉", "诊断"};
    for(unsigned index = 0; index < 3; ++index) {
        nav_buttons_[index] = theme::create_button(bar, names[index], 104, lv_color_hex(0x2B3844));
        lv_obj_add_event_cb(nav_buttons_[index], nav_event, LV_EVENT_CLICKED, this);
    }

    status_label_ = theme::create_label(bar, "FPS --  RSS --", lv_color_hex(0xD7E0E6));
    lv_obj_set_width(status_label_, 570);
    lv_obj_set_style_pad_left(status_label_, 12, 0);
    lv_obj_set_style_pad_top(status_label_, 10, 0);
}

void PocUi::create_interaction_page(lv_obj_t* parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(parent, 8, 0);

    lv_obj_t* controls = panel(parent, 302);
    theme::create_label(controls, "控件响应", theme::ink());
    lv_obj_t* row = lv_obj_create(controls);
    lv_obj_set_size(row, 280, 46);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 16, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_t* tap = theme::create_button(row, "快速点击", 138, theme::accent());
    lv_obj_add_event_cb(tap, interaction_event, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(tap, interaction_event, LV_EVENT_LONG_PRESSED, this);
    lv_obj_t* toggle = lv_switch_create(row);
    lv_obj_set_size(toggle, 72, 36);
    lv_obj_set_style_bg_color(toggle, theme::accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);

    lv_obj_t* slider = lv_slider_create(controls);
    lv_obj_set_size(slider, 278, 20);
    lv_slider_set_value(slider, 42, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, theme::accent(), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider, slider_event, LV_EVENT_VALUE_CHANGED, this);
    slider_label_ = theme::create_label(controls, "滑块 42%", theme::ink());
    interaction_label_ = theme::create_label(controls, "等待点击或长按", theme::muted());

    lv_obj_t* scrolling = panel(parent, 318);
    theme::create_label(scrolling, "横向滚动", theme::ink());
    lv_obj_t* strip = lv_obj_create(scrolling);
    lv_obj_set_size(strip, 294, 72);
    lv_obj_set_style_pad_all(strip, 8, 0);
    lv_obj_set_style_pad_gap(strip, 8, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
    for(int index = 1; index <= 7; ++index) {
        char caption[16] {};
        std::snprintf(caption, sizeof(caption), "项目 %d", index);
        lv_obj_t* chip = theme::create_button(strip, caption, 92,
                                              index % 2 == 0 ? theme::danger() : theme::accent());
        lv_obj_set_height(chip, 42);
    }
    theme::create_label(scrolling, "连续滑动，验证边缘跟踪", theme::muted());

    lv_obj_t* dragging = panel(parent, 316);
    theme::create_label(dragging, "拖动与手势", theme::ink());
    lv_obj_t* zone = lv_obj_create(dragging);
    lv_obj_set_size(zone, 292, 112);
    lv_obj_set_style_bg_color(zone, lv_color_hex(0xE8F1F2), 0);
    lv_obj_set_style_border_color(zone, theme::line(), 0);
    lv_obj_set_style_pad_all(zone, 0, 0);
    lv_obj_remove_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* draggable = theme::create_button(zone, "拖动", 86, theme::accent());
    lv_obj_set_pos(draggable, 18, 30);
    lv_obj_add_event_cb(draggable, drag_event, LV_EVENT_PRESSING, this);
}

void PocUi::create_visual_page(lv_obj_t* parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(parent, 8, 0);

    lv_obj_t* type = panel(parent, 330);
    lv_obj_t* title = theme::create_label(type, "LVGL Interface", theme::accent());
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    theme::create_label(type, "中文显示：词典、学习、交互、诊断", theme::ink());
    theme::create_label(type, "English text  Aa Bb  0123456789", theme::ink());
    theme::create_label(type, "Source Han Sans SC 16", theme::muted());

    lv_obj_t* colors = panel(parent, 614);
    theme::create_label(colors, "颜色、圆角与局部动画", theme::ink());
    lv_obj_t* row = lv_obj_create(colors);
    lv_obj_set_size(row, 590, 108);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    const uint32_t values[] {0x00897B, 0xD94F45, 0xE8B83D, 0x2D7FF9, 0x17212B};
    for(unsigned index = 0; index < 5; ++index) {
        lv_obj_t* swatch = lv_obj_create(row);
        lv_obj_set_size(swatch, 104, 92);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(values[index]), 0);
        lv_obj_set_style_bg_grad_color(swatch, lv_color_white(), 0);
        lv_obj_set_style_bg_grad_dir(swatch, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_radius(swatch, index == 0 ? 46 : 6, 0);
        lv_obj_set_style_border_width(swatch, 0, 0);
    }
}

void PocUi::create_diagnostics_page(lv_obj_t* parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(parent, 8, 0);

    lv_obj_t* live = panel(parent, 520);
    theme::create_label(live, "实时指标", theme::ink());
    diagnostics_label_ = theme::create_label(live, "等待第一帧...", theme::muted());

    lv_obj_t* contract = panel(parent, 424);
    theme::create_label(contract, "共享运行时", theme::ink());
    theme::create_label(contract, "DRM / evdev / LVGL 统一初始化", theme::muted());
    theme::create_label(contract, "下拉状态栏由 app_shell 提供", theme::muted());
    theme::create_label(contract, "异常退出由 session 恢复启动器", theme::muted());
}

void PocUi::show_page(unsigned index)
{
    if(index >= 3) return;
    for(unsigned current = 0; current < 3; ++current) {
        if(current == index) {
            lv_obj_remove_flag(pages_[current], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(nav_buttons_[current], theme::accent(), 0);
        }
        else {
            lv_obj_add_flag(pages_[current], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(nav_buttons_[current], lv_color_hex(0x2B3844), 0);
        }
    }
}

void PocUi::nav_event(lv_event_t* event)
{
    auto* self = static_cast<PocUi*>(lv_event_get_user_data(event));
    lv_obj_t* target = lv_event_get_target_obj(event);
    for(unsigned index = 0; index < 3; ++index) {
        if(target == self->nav_buttons_[index]) self->show_page(index);
    }
}

void PocUi::interaction_event(lv_event_t* event)
{
    auto* self = static_cast<PocUi*>(lv_event_get_user_data(event));
    if(lv_event_get_code(event) == LV_EVENT_LONG_PRESSED) {
        lv_label_set_text(self->interaction_label_, "长按识别成功");
    }
    else {
        ++self->tap_count_;
        lv_label_set_text_fmt(self->interaction_label_, "快速点击 %u 次", self->tap_count_);
    }
}

void PocUi::slider_event(lv_event_t* event)
{
    auto* self = static_cast<PocUi*>(lv_event_get_user_data(event));
    lv_label_set_text_fmt(self->slider_label_, "滑块 %d%%",
                          static_cast<int>(lv_slider_get_value(lv_event_get_target_obj(event))));
}

void PocUi::drag_event(lv_event_t* event)
{
    lv_obj_t* object = lv_event_get_target_obj(event);
    lv_point_t vector {};
    lv_indev_get_vect(lv_indev_active(), &vector);
    const int32_t x = std::clamp(lv_obj_get_x(object) + vector.x, int32_t {0}, int32_t {206});
    const int32_t y = std::clamp(lv_obj_get_y(object) + vector.y, int32_t {0}, int32_t {68});
    lv_obj_set_pos(object, x, y);
}

void PocUi::status_timer(lv_timer_t* timer)
{
    auto* self = static_cast<PocUi*>(lv_timer_get_user_data(timer));
    const MetricsSnapshot metrics = self->metrics_.snapshot();
    lv_label_set_text_fmt(self->status_label_, "FPS %.1f   CPU %.0f%%   RSS %.1f MB",
                          metrics.fps, metrics.cpu_percent,
                          static_cast<double>(metrics.rss_kb) / 1024.0);
    lv_label_set_text_fmt(self->diagnostics_label_,
                          "FPS %.1f   平均帧 %.2f ms   峰值 %.2f ms\n"
                          "CPU %.1f%%   RSS %.2f MB   总帧 %llu\n"
                          "触摸 raw(%d,%d) -> logical(%d,%d)   events %llu",
                          metrics.fps, metrics.average_frame_ms, metrics.peak_frame_ms,
                          metrics.cpu_percent, static_cast<double>(metrics.rss_kb) / 1024.0,
                          static_cast<unsigned long long>(metrics.frames),
                          metrics.pointer.raw_x, metrics.pointer.raw_y,
                          metrics.pointer.point.x, metrics.pointer.point.y,
                          static_cast<unsigned long long>(metrics.input_events));
}

bool PocUi::exit_requested() const
{
    return false;
}

}  // namespace dictpen
