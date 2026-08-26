#pragma once

#include <lvgl.h>

namespace dictpen::theme {

constexpr int32_t screen_width = 960;
constexpr int32_t screen_height = 266;
constexpr int32_t gesture_height = 18;
constexpr int32_t panel_height = 82;
constexpr int32_t touch_target = 44;

lv_color_t ink();
lv_color_t muted();
lv_color_t canvas();
lv_color_t surface();
lv_color_t line();
lv_color_t accent();
lv_color_t accent_pressed();
lv_color_t danger();

const lv_font_t* body_font();
void style_screen(lv_obj_t* object);
void style_label(lv_obj_t* object, lv_color_t color);
void style_button(lv_obj_t* object, lv_color_t background);
lv_obj_t* create_label(lv_obj_t* parent, const char* text, lv_color_t color);
lv_obj_t* create_button(lv_obj_t* parent, const char* text, int32_t width, lv_color_t background);

}  // namespace dictpen::theme
