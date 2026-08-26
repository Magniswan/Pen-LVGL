#include <lvgl_platform/sdk.hpp>

#include <lvgl.h>

namespace {

class ExampleApplication final : public dictpen::RuntimeApplication {
public:
    void create(dictpen::RuntimeContext& context) override
    {
        control_ = &context.control;
        shell_ = new dictpen::AppShell(context.control, {"示例应用", false});
        shell_->create();

        auto* screen = lv_screen_active();
        dictpen::theme::style_screen(screen);
        auto* card = lv_obj_create(screen);
        lv_obj_set_size(card, 650, 150);
        lv_obj_center(card);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xDCE6E7), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 24, 0);

        auto* title = lv_label_create(card);
        lv_label_set_text(title, "Hello, LVGL");
        lv_obj_set_style_text_color(title, lv_color_hex(0x172033), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

        auto* detail = lv_label_create(card);
        lv_label_set_text(detail, "从这里开始构建你的词典笔应用");
        lv_obj_set_style_text_color(detail, lv_color_hex(0x294C60), 0);
        lv_obj_set_style_text_font(detail, dictpen::theme::body_font(), 0);
        lv_obj_align(detail, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    bool stop_requested() const override
    {
        return shell_ != nullptr && shell_->stop_requested();
    }

    void destroy() override
    {
        if(shell_ != nullptr) {
            shell_->destroy();
            delete shell_;
            shell_ = nullptr;
        }
        control_ = nullptr;
    }

private:
    dictpen::AppControl* control_ {nullptr};
    dictpen::AppShell* shell_ {nullptr};
};

}  // namespace

int main()
{
    ExampleApplication application;
    return dictpen::run_platform_application(
        application, {LVGL_APPLICATION_ID, "LVGL_RUN_SECONDS", 0, nullptr, false});
}
