#include "app/poc_ui.h"
#include "assets/poc_font.h"

#include <algorithm>
#include <cstdio>

namespace dictpen {
namespace {

constexpr lv_color_t kInk = LV_COLOR_MAKE(24, 32, 42);
constexpr lv_color_t kMuted = LV_COLOR_MAKE(92, 104, 116);
constexpr lv_color_t kSurface = LV_COLOR_MAKE(247, 249, 250);
constexpr lv_color_t kLine = LV_COLOR_MAKE(220, 225, 229);
constexpr lv_color_t kTeal = LV_COLOR_MAKE(0, 137, 123);
constexpr lv_color_t kCoral = LV_COLOR_MAKE(232, 93, 76);
constexpr lv_color_t kYellow = LV_COLOR_MAKE(246, 190, 50);

void set_text_style(lv_obj_t* object, lv_color_t color)
{
    lv_obj_set_style_text_font(object, &lv_font_poc_16, 0);
    lv_obj_set_style_text_color(object, color, 0);
}

}  // namespace

PocUi::PocUi(RuntimeMetrics& metrics)
    : metrics_(metrics)
{
}

lv_obj_t* PocUi::label(lv_obj_t* parent, const char* text)
{
    lv_obj_t* value = lv_label_create(parent);
    lv_label_set_text(value, text);
    set_text_style(value, kInk);
    return value;
}

lv_obj_t* PocUi::button(lv_obj_t* parent, const char* text, int32_t width)
{
    lv_obj_t* value = lv_button_create(parent);
    lv_obj_set_size(value, width, 44);
    lv_obj_set_style_radius(value, 6, 0);
    lv_obj_set_style_bg_color(value, kTeal, 0);
    lv_obj_set_style_bg_color(value, lv_color_hex(0x00695C), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(value, 0, 0);
    lv_obj_t* caption = label(value, text);
    lv_obj_set_style_text_color(caption, lv_color_white(), 0);
    lv_obj_center(caption);
    return value;
}

lv_obj_t* PocUi::panel(lv_obj_t* parent, int32_t width)
{
    lv_obj_t* value = lv_obj_create(parent);
    lv_obj_set_size(value, width, 198);
    lv_obj_set_style_radius(value, 6, 0);
    lv_obj_set_style_bg_color(value, lv_color_white(), 0);
    lv_obj_set_style_border_color(value, kLine, 0);
    lv_obj_set_style_border_width(value, 1, 0);
    lv_obj_set_style_pad_all(value, 12, 0);
    lv_obj_set_style_pad_gap(value, 8, 0);
    lv_obj_set_flex_flow(value, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(value, LV_OBJ_FLAG_SCROLLABLE);
    return value;
}

void PocUi::create()
{
    lv_obj_t* root = lv_screen_active();
    lv_obj_set_style_bg_color(root, kSurface, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    set_text_style(root, kInk);

    create_top_bar(root);
    lv_obj_t* content = lv_obj_create(root);
    lv_obj_set_pos(content, 0, 54);
    lv_obj_set_size(content, 960, 212);
    lv_obj_set_style_bg_color(content, kSurface, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 7, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    for(auto& page : pages_) {
        page = lv_obj_create(content);
        lv_obj_set_pos(page, 0, 0);
        lv_obj_set_size(page, 946, 198);
        lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 0, 0);
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    }

    create_interaction_page(pages_[0]);
    create_visual_page(pages_[1]);
    create_diagnostics_page(pages_[2]);
    show_page(0);
    lv_timer_create(status_timer, 500, this);
}

void PocUi::create_top_bar(lv_obj_t* root)
{
    lv_obj_t* bar = lv_obj_create(root);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, 960, 54);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x18202A), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 5, 0);
    lv_obj_set_style_pad_gap(bar, 6, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    const char* names[] {"交互", "视觉", "诊断"};
    for(unsigned index = 0; index < 3; ++index) {
        nav_buttons_[index] = button(bar, names[index], 104);
        lv_obj_set_height(nav_buttons_[index], 44);
        lv_obj_set_style_bg_color(nav_buttons_[index], lv_color_hex(0x273340), 0);
        lv_obj_add_event_cb(nav_buttons_[index], nav_event, LV_EVENT_CLICKED, this);
    }

    status_label_ = label(bar, "FPS --  RSS --");
    lv_obj_set_width(status_label_, 475);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xD8E2E8), 0);
    lv_obj_set_style_pad_left(status_label_, 10, 0);

    lv_obj_t* pulse = lv_obj_create(bar);
    lv_obj_set_size(pulse, 42, 7);
    lv_obj_set_style_radius(pulse, 4, 0);
    lv_obj_set_style_bg_color(pulse, kYellow, 0);
    lv_obj_set_style_border_width(pulse, 0, 0);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, pulse);
    lv_anim_set_values(&animation, 0, 40);
    lv_anim_set_duration(&animation, 900);
    lv_anim_set_reverse_duration(&animation, 900);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&animation, animate_x);
    lv_anim_start(&animation);

    lv_obj_t* exit = button(bar, "退出", 78);
    lv_obj_set_height(exit, 44);
    lv_obj_set_style_bg_color(exit, kCoral, 0);
    lv_obj_add_event_cb(exit, exit_event, LV_EVENT_CLICKED, this);
}

void PocUi::create_interaction_page(lv_obj_t* parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(parent, 8, 0);

    lv_obj_t* controls = panel(parent, 300);
    label(controls, "控件响应");
    lv_obj_t* row = lv_obj_create(controls);
    lv_obj_set_size(row, 274, 46);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_t* tap = button(row, "快速点击", 130);
    lv_obj_add_event_cb(tap, interaction_event, LV_EVENT_CLICKED, this);
    lv_obj_t* toggle = lv_switch_create(row);
    lv_obj_set_size(toggle, 72, 36);
    lv_obj_set_style_bg_color(toggle, kTeal, LV_PART_INDICATOR | LV_STATE_CHECKED);

    lv_obj_t* slider = lv_slider_create(controls);
    lv_obj_set_size(slider, 274, 22);
    lv_slider_set_value(slider, 42, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, kTeal, LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider, slider_event, LV_EVENT_VALUE_CHANGED, this);
    slider_label_ = label(controls, "滑块 42%");
    interaction_label_ = label(controls, "等待点击或长按");
    lv_obj_add_event_cb(tap, interaction_event, LV_EVENT_LONG_PRESSED, this);

    lv_obj_t* scrolling = panel(parent, 312);
    label(scrolling, "横向滚动");
    lv_obj_t* strip = lv_obj_create(scrolling);
    lv_obj_set_size(strip, 286, 70);
    lv_obj_set_style_pad_all(strip, 8, 0);
    lv_obj_set_style_pad_gap(strip, 8, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
    for(int index = 1; index <= 7; ++index) {
        char caption[16];
        std::snprintf(caption, sizeof(caption), "项目 %d", index);
        lv_obj_t* chip = button(strip, caption, 92);
        lv_obj_set_height(chip, 42);
        lv_obj_set_style_bg_color(chip, index % 2 == 0 ? kCoral : kTeal, 0);
    }
    label(scrolling, "可连续滑动，验证边缘跟踪");

    lv_obj_t* dragging = panel(parent, 318);
    label(dragging, "拖动与手势");
    lv_obj_t* zone = lv_obj_create(dragging);
    lv_obj_set_size(zone, 292, 112);
    lv_obj_set_style_bg_color(zone, lv_color_hex(0xEAF1F3), 0);
    lv_obj_set_style_border_color(zone, kLine, 0);
    lv_obj_set_style_pad_all(zone, 0, 0);
    lv_obj_remove_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* draggable = button(zone, "拖动", 86);
    lv_obj_set_pos(draggable, 18, 30);
    lv_obj_add_event_cb(draggable, drag_event, LV_EVENT_PRESSING, this);
}

void PocUi::create_visual_page(lv_obj_t* parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(parent, 8, 0);

    lv_obj_t* typography = panel(parent, 330);
    lv_obj_t* title = label(typography, "LVGL Interface");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, kTeal, 0);
    label(typography, "中文显示：词典、学习、交互、诊断");
    label(typography, "English text  Aa Bb  0123456789");
    lv_obj_t* muted = label(typography, "CJK subset 16 / Montserrat 24");
    lv_obj_set_style_text_color(muted, kMuted, 0);

    lv_obj_t* image_panel = panel(parent, 270);
    label(image_panel, "PNG 解码与透明度");
    lv_obj_t* image = lv_image_create(image_panel);
    lv_image_set_src(image, "A:/tmp/lvgl-poc-logo.png");
    lv_image_set_scale(image, 150);
    lv_obj_set_style_opa(image, LV_OPA_80, 0);
    lv_obj_center(image);

    lv_obj_t* shapes = panel(parent, 330);
    label(shapes, "圆角、渐变、局部动画");
    lv_obj_t* shape_row = lv_obj_create(shapes);
    lv_obj_set_size(shape_row, 304, 70);
    lv_obj_set_style_bg_opa(shape_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shape_row, 0, 0);
    lv_obj_set_style_pad_all(shape_row, 0, 0);
    lv_obj_set_style_pad_gap(shape_row, 12, 0);
    lv_obj_set_flex_flow(shape_row, LV_FLEX_FLOW_ROW);
    const lv_color_t colors[] {kTeal, kCoral, kYellow};
    for(unsigned index = 0; index < 3; ++index) {
        lv_obj_t* shape = lv_obj_create(shape_row);
        lv_obj_set_size(shape, 82, 62);
        lv_obj_set_style_radius(shape, index == 0 ? 31 : 8, 0);
        lv_obj_set_style_bg_color(shape, colors[index], 0);
        lv_obj_set_style_bg_grad_color(shape, colors[(index + 1) % 3], 0);
        lv_obj_set_style_bg_grad_dir(shape, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(shape, index == 1 ? LV_OPA_60 : LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(shape, 0, 0);
    }
    label(shapes, "顶部黄色指示条持续动画");
}

void PocUi::create_diagnostics_page(lv_obj_t* parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(parent, 8, 0);

    lv_obj_t* live = panel(parent, 430);
    label(live, "实时指标");
    diagnostics_label_ = label(live, "等待第一帧...");
    lv_obj_set_style_text_color(diagnostics_label_, kMuted, 0);

    lv_obj_t* colors = panel(parent, 508);
    label(colors, "颜色与刷新路径");
    lv_obj_t* row = lv_obj_create(colors);
    lv_obj_set_size(row, 482, 112);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    const uint32_t values[] {0x000000, 0xFFFFFF, 0xE84F4F, 0x27AE60, 0x2D7FF9, 0xF6BE32};
    for(uint32_t value : values) {
        lv_obj_t* swatch = lv_obj_create(row);
        lv_obj_set_size(swatch, 72, 92);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(value), 0);
        lv_obj_set_style_border_color(swatch, kLine, 0);
        lv_obj_set_style_border_width(swatch, 1, 0);
        lv_obj_set_style_radius(swatch, 5, 0);
    }
}

void PocUi::show_page(unsigned index)
{
    if(index >= 3) return;
    for(unsigned current = 0; current < 3; ++current) {
        if(current == index) {
            lv_obj_remove_flag(pages_[current], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(nav_buttons_[current], kTeal, 0);
        }
        else {
            lv_obj_add_flag(pages_[current], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(nav_buttons_[current], lv_color_hex(0x273340), 0);
        }
    }
    std::printf("UI page=%u\n", index);
    std::fflush(stdout);
}

void PocUi::nav_event(lv_event_t* event)
{
    auto* self = static_cast<PocUi*>(lv_event_get_user_data(event));
    lv_obj_t* target = lv_event_get_target_obj(event);
    for(unsigned index = 0; index < 3; ++index) {
        if(target == self->nav_buttons_[index]) self->show_page(index);
    }
}

void PocUi::exit_event(lv_event_t* event)
{
    auto* self = static_cast<PocUi*>(lv_event_get_user_data(event));
    lv_obj_t* box = lv_msgbox_create(lv_layer_top());
    lv_msgbox_add_title(box, "退出测试？");
    lv_msgbox_add_text(box, "退出后将自动恢复 Falcon。确认退出？");
    lv_obj_t* cancel = lv_msgbox_add_footer_button(box, "取消");
    lv_obj_t* confirm = lv_msgbox_add_footer_button(box, "确认");
    lv_obj_add_event_cb(cancel, confirm_exit_event, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(confirm, confirm_exit_event, LV_EVENT_CLICKED, self);
    lv_obj_set_width(box, 430);
    set_text_style(box, kInk);
}

void PocUi::confirm_exit_event(lv_event_t* event)
{
    auto* self = static_cast<PocUi*>(lv_event_get_user_data(event));
    if(self) self->exit_requested_ = true;
    lv_obj_t* button_object = lv_event_get_target_obj(event);
    lv_obj_t* box = lv_obj_get_parent(lv_obj_get_parent(button_object));
    lv_msgbox_close(box);
}

void PocUi::interaction_event(lv_event_t* event)
{
    auto* self = static_cast<PocUi*>(lv_event_get_user_data(event));
    if(lv_event_get_code(event) == LV_EVENT_LONG_PRESSED) {
        lv_label_set_text(self->interaction_label_, "长按识别成功");
        std::printf("UI event=long_press\n");
    }
    else {
        ++self->tap_count_;
        lv_label_set_text_fmt(self->interaction_label_, "快速点击 %u 次", self->tap_count_);
        std::printf("UI event=tap count=%u\n", self->tap_count_);
    }
    std::fflush(stdout);
}

void PocUi::slider_event(lv_event_t* event)
{
    auto* self = static_cast<PocUi*>(lv_event_get_user_data(event));
    lv_obj_t* slider = lv_event_get_target_obj(event);
    lv_label_set_text_fmt(self->slider_label_, "滑块 %d%%", static_cast<int>(lv_slider_get_value(slider)));
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
    char status[96] {};
    std::snprintf(status, sizeof(status), "FPS %.1f   CPU %.0f%%   RSS %.1f MB",
                  metrics.fps, metrics.cpu_percent,
                  static_cast<double>(metrics.rss_kb) / 1024.0);
    lv_label_set_text(self->status_label_, status);

    char diagnostics[320] {};
    std::snprintf(diagnostics, sizeof(diagnostics),
                  "FPS %.1f   平均帧 %.2f ms   峰值 %.2f ms\n"
                  "CPU %.1f%%   RSS %.2f MB   峰值 %.2f MB   总帧 %llu\n"
                  "触摸 raw(%d,%d) -> logical(%d,%d)  events %llu",
                  metrics.fps, metrics.average_frame_ms, metrics.peak_frame_ms,
                  metrics.cpu_percent,
                  static_cast<double>(metrics.rss_kb) / 1024.0,
                  static_cast<double>(metrics.peak_rss_kb) / 1024.0,
                  static_cast<unsigned long long>(metrics.frames),
                  metrics.pointer.raw_x, metrics.pointer.raw_y,
                  metrics.pointer.point.x, metrics.pointer.point.y,
                  static_cast<unsigned long long>(metrics.input_events));
    lv_label_set_text(self->diagnostics_label_, diagnostics);
}

void PocUi::animate_x(void* object, int32_t value)
{
    lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(object), value, 0);
}

bool PocUi::exit_requested() const
{
    return exit_requested_;
}

}  // namespace dictpen
