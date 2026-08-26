#pragma once

#include "game_2048_model.h"

#include <array>
#include <optional>

#include <lvgl.h>

namespace dictpen { class AppStorage; }

namespace lvgl_apps {

class Game2048Ui {
public:
    Game2048Ui(std::uint64_t seed, dictpen::AppStorage* storage) noexcept;

    void create();
    void destroy();
    bool stop_requested() const noexcept { return false; }

#if defined(LVGL_PLATFORM_VISUAL_TESTING)
    void visual_test_set_board(
        const std::array<std::uint16_t, 16>& board, std::uint32_t score = 0) noexcept;
    void visual_test_request_move(MoveDirection direction);
    bool visual_test_geometry_valid() const noexcept;
#endif

private:
    static void gesture_event(lv_event_t* event);
    static void undo_event(lv_event_t* event);
    static void restart_event(lv_event_t* event);
    static void confirm_restart_event(lv_event_t* event);
    static void cancel_restart_event(lv_event_t* event);
    static void animate_translate_y(void* object, std::int32_t value);
    static void animate_scale(void* object, std::int32_t value);
    static void animate_motion(void* object, std::int32_t value);
    static void motion_animation_complete(lv_anim_t* animation);
    static void resolution_animation_complete(lv_anim_t* animation);

    void render(bool entering = false);
    void set_tile_value(lv_obj_t* tile, lv_obj_t* label, std::uint16_t value);
    void request_move(MoveDirection direction);
    void run_move(MoveDirection direction);
    void begin_motion(const MoveOutcome& outcome);
    void complete_motion_phase();
    void start_resolution_effects();
    void clear_motion_visuals();
    void finish_move();
    void persist() noexcept;
    void show_restart_confirmation();
    void update_phase();
    static lv_color_t tile_color(std::uint16_t value) noexcept;
    static lv_color_t tile_text_color(std::uint16_t value) noexcept;

    struct MotionVisual {
        lv_obj_t* tile {nullptr};
        lv_obj_t* label {nullptr};
        std::int32_t from_x {0};
        std::int32_t from_y {0};
        std::int32_t to_x {0};
        std::int32_t to_y {0};
    };

    Game2048 game_;
    dictpen::AppStorage* storage_ {nullptr};
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
    std::array<MotionVisual, 16> motion_visuals_ {};
    std::size_t pending_motion_animations_ {0};
    std::optional<MoveOutcome> pending_outcome_;
    std::optional<MoveDirection> queued_move_;
};

}  // namespace lvgl_apps
