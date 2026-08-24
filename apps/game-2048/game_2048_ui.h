#pragma once

#include "game_2048_model.h"

#include <array>
#include <optional>

#include <lvgl.h>

namespace lvgl_apps {

class Game2048Ui {
public:
    explicit Game2048Ui(std::uint64_t seed) noexcept;

    void create();
    void destroy();
    bool stop_requested() const noexcept { return false; }

private:
    static void gesture_event(lv_event_t* event);
    static void undo_event(lv_event_t* event);
    static void restart_event(lv_event_t* event);
    static void confirm_restart_event(lv_event_t* event);
    static void cancel_restart_event(lv_event_t* event);
    static void animate_translate_x(void* object, std::int32_t value);
    static void animate_translate_y(void* object, std::int32_t value);
    static void animate_scale(void* object, std::int32_t value);
    static void move_animation_complete(lv_anim_t* animation);

    void render(bool entering = false);
    void request_move(MoveDirection direction);
    void run_move(MoveDirection direction);
    void finish_move();
    void show_restart_confirmation();
    void update_phase();
    static lv_color_t tile_color(std::uint16_t value) noexcept;
    static lv_color_t tile_text_color(std::uint16_t value) noexcept;

    Game2048 game_;
    lv_obj_t* board_ {nullptr};
    std::array<lv_obj_t*, 16> tiles_ {};
    std::array<lv_obj_t*, 16> labels_ {};
    lv_obj_t* score_label_ {nullptr};
    lv_obj_t* best_label_ {nullptr};
    lv_obj_t* status_label_ {nullptr};
    lv_obj_t* score_fly_ {nullptr};
    lv_obj_t* restart_box_ {nullptr};
    lv_point_t gesture_start_ {};
    bool animating_ {false};
    bool reduced_motion_ {false};
    std::optional<MoveDirection> queued_move_;
};

}  // namespace lvgl_apps
