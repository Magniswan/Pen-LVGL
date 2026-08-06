#include "shell/app_theme.h"

namespace dictpen::theme {

lv_color_t ink() { return lv_color_hex(0x17212B); }
lv_color_t muted() { return lv_color_hex(0x66727D); }
lv_color_t canvas() { return lv_color_hex(0xF3F5F6); }
lv_color_t surface() { return lv_color_hex(0xFFFFFF); }
lv_color_t line() { return lv_color_hex(0xD8DEE3); }
lv_color_t accent() { return lv_color_hex(0x00897B); }
lv_color_t accent_pressed() { return lv_color_hex(0x00695C); }
lv_color_t danger() { return lv_color_hex(0xD94F45); }

const lv_font_t* body_font()
{
    return &lv_font_source_han_sans_sc_16_cjk;
}

void style_screen(lv_obj_t* object)
{
    lv_obj_set_style_bg_color(object, canvas(), 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(object, body_font(), 0);
    lv_obj_set_style_text_color(object, ink(), 0);
}

void style_label(lv_obj_t* object, lv_color_t color)
{
    lv_obj_set_style_text_font(object, body_font(), 0);
    lv_obj_set_style_text_color(object, color, 0);
    lv_obj_set_style_text_letter_space(object, 0, 0);
}

void style_button(lv_obj_t* object, lv_color_t background)
{
    lv_obj_set_style_radius(object, 6, 0);
    lv_obj_set_style_bg_color(object, background, 0);
    lv_obj_set_style_bg_color(object, accent_pressed(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

lv_obj_t* create_label(lv_obj_t* parent, const char* text, lv_color_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    style_label(label, color);
    return label;
}

lv_obj_t* create_button(lv_obj_t* parent, const char* text, int32_t width, lv_color_t background)
{
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, width, touch_target);
    style_button(button, background);
    lv_obj_t* label = create_label(button, text, lv_color_white());
    lv_obj_center(label);
    return button;
}

}  // namespace dictpen::theme
