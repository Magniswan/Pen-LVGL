#include "game_2048_ui.h"

#include "game_2048_persistence.h"

#include <lvgl_platform/app_storage.hpp>
#include <lvgl_platform/theme.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace lvgl_apps {
namespace {

constexpr std::int32_t kBoardSize = 236;
constexpr std::int32_t kTileSize = 51;
constexpr std::int32_t kGap = 6;
constexpr std::int32_t kOrigin = 7;

std::int32_t tile_x(std::uint8_t cell) noexcept
{
    return kOrigin + static_cast<std::int32_t>(cell % 4U) * (kTileSize + kGap);
}

std::int32_t tile_y(std::uint8_t cell) noexcept
{
    return kOrigin + static_cast<std::int32_t>(cell / 4U) * (kTileSize + kGap);
}

lv_obj_t* label(lv_obj_t* parent, const char* text, lv_color_t color)
{
    auto* object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_style_text_color(object, color, 0);
    lv_obj_set_style_text_font(object, dictpen::theme::body_font(), 0);
    return object;
}

lv_obj_t* action(lv_obj_t* parent, const char* text, std::int32_t x)
{
    auto* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, 184);
    lv_obj_set_size(button, 126, 48);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x203638), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x315551), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x50706C), 0);
    auto* text_label = label(button, text, lv_color_hex(0xE8F0EC));
    lv_obj_center(text_label);
    return button;
}

}  // namespace

Game2048Ui::Game2048Ui(std::uint64_t seed, dictpen::AppStorage* storage) noexcept
    : game_(seed), storage_(storage)
{
}

void Game2048Ui::create()
{
    if(storage_ != nullptr && storage_->available()) load_game_state(*storage_, game_);
    reduced_motion_ = std::getenv("LVGL_REDUCED_MOTION") != nullptr &&
                      std::string_view(std::getenv("LVGL_REDUCED_MOTION")) == "1";
    auto* root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x10191B), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, gesture_event, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(root, gesture_event, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(root, gesture_event, LV_EVENT_PRESS_LOST, this);

    auto* eyebrow = label(root, "MINERAL  /  2048", lv_color_hex(0x6FC4B5));
    lv_obj_set_pos(eyebrow, 28, 24);
    lv_obj_set_style_text_font(eyebrow, &lv_font_montserrat_16, 0);
    auto* title = label(root, "MERGE MATRIX", lv_color_hex(0xF2F5F1));
    lv_obj_set_pos(title, 28, 54);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    auto* hint = label(root, "SWIPE  /  MATCH TO MERGE", lv_color_hex(0x879895));
    lv_obj_set_pos(hint, 28, 92);

    auto* score_card = lv_obj_create(root);
    lv_obj_set_pos(score_card, 28, 122);
    lv_obj_set_size(score_card, 126, 50);
    lv_obj_set_style_radius(score_card, 12, 0);
    lv_obj_set_style_bg_color(score_card, lv_color_hex(0x192729), 0);
    lv_obj_set_style_border_width(score_card, 1, 0);
    lv_obj_set_style_border_color(score_card, lv_color_hex(0x344C4B), 0);
    lv_obj_remove_flag(score_card, LV_OBJ_FLAG_SCROLLABLE);
    score_label_ = label(score_card, "0", lv_color_hex(0xF3C47A));
    lv_obj_align(score_label_, LV_ALIGN_CENTER, 0, 3);

    auto* best_card = lv_obj_create(root);
    lv_obj_set_pos(best_card, 166, 122);
    lv_obj_set_size(best_card, 126, 50);
    lv_obj_set_style_radius(best_card, 12, 0);
    lv_obj_set_style_bg_color(best_card, lv_color_hex(0x192729), 0);
    lv_obj_set_style_border_width(best_card, 1, 0);
    lv_obj_set_style_border_color(best_card, lv_color_hex(0x344C4B), 0);
    lv_obj_remove_flag(best_card, LV_OBJ_FLAG_SCROLLABLE);
    best_label_ = label(best_card, "BEST  0", lv_color_hex(0xC8D4D0));
    lv_obj_center(best_label_);

    auto* undo = action(root, "UNDO", 28);
    lv_obj_add_event_cb(undo, undo_event, LV_EVENT_CLICKED, this);
    auto* restart = action(root, "RESET", 166);
    lv_obj_add_event_cb(restart, restart_event, LV_EVENT_CLICKED, this);

    board_ = lv_obj_create(root);
    lv_obj_set_pos(board_, 354, 15);
    lv_obj_set_size(board_, kBoardSize, kBoardSize);
    lv_obj_set_style_radius(board_, 18, 0);
    lv_obj_set_style_bg_color(board_, lv_color_hex(0x1B292B), 0);
    lv_obj_set_style_border_width(board_, 1, 0);
    lv_obj_set_style_border_color(board_, lv_color_hex(0x45615E), 0);
    lv_obj_set_style_pad_all(board_, 0, 0);
    lv_obj_set_style_shadow_width(board_, 20, 0);
    lv_obj_set_style_shadow_color(board_, lv_color_hex(0x071011), 0);
    lv_obj_set_style_shadow_opa(board_, LV_OPA_60, 0);
    lv_obj_remove_flag(board_, LV_OBJ_FLAG_SCROLLABLE);

    for(std::size_t index = 0; index < tiles_.size(); ++index) {
        auto* tile = lv_obj_create(board_);
        const auto column = static_cast<std::int32_t>(index % 4);
        const auto row = static_cast<std::int32_t>(index / 4);
        lv_obj_set_pos(tile, kOrigin + column * (kTileSize + kGap),
                      kOrigin + row * (kTileSize + kGap));
        lv_obj_set_size(tile, kTileSize, kTileSize);
        lv_obj_set_style_radius(tile, 11, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x526B66), 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        tiles_[index] = tile;
        labels_[index] = label(tile, "", lv_color_white());
        lv_obj_center(labels_[index]);
    }

    status_label_ = label(root, "", lv_color_hex(0xD9E5E0));
    lv_obj_set_pos(status_label_, 626, 78);
    lv_obj_set_width(status_label_, 300);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_24, 0);
    score_fly_ = label(root, "", lv_color_hex(0xF3C47A));
    lv_obj_set_pos(score_fly_, 626, 128);
    lv_obj_set_style_text_font(score_fly_, &lv_font_montserrat_24, 0);
    render(true);
}

void Game2048Ui::destroy()
{
    persist();
    clear_motion_visuals();
    for(auto* tile : tiles_) if(tile) lv_anim_delete(tile, animate_scale);
    if(score_fly_) lv_anim_delete(score_fly_, animate_translate_y);
    pending_outcome_.reset();
    queued_move_.reset();
    board_ = nullptr;
    restart_box_ = nullptr;
}

#if defined(LVGL_PLATFORM_VISUAL_TESTING)
void Game2048Ui::visual_test_set_board(
    const std::array<std::uint16_t, 16>& board, std::uint32_t score) noexcept
{
    game_.set_board_for_test(board, score);
}

void Game2048Ui::visual_test_request_move(MoveDirection direction)
{
    request_move(direction);
}

bool Game2048Ui::visual_test_geometry_valid() const noexcept
{
    if(board_ == nullptr) return false;
    lv_area_t board_area {};
    lv_obj_get_coords(board_, &board_area);
    for(const auto* tile : tiles_) {
        if(tile == nullptr) return false;
        lv_area_t tile_area {};
        lv_obj_get_coords(const_cast<lv_obj_t*>(tile), &tile_area);
        if(tile_area.x1 <= board_area.x1 || tile_area.y1 <= board_area.y1 ||
           tile_area.x2 >= board_area.x2 || tile_area.y2 >= board_area.y2) {
            return false;
        }
    }
    return true;
}
#endif

lv_color_t Game2048Ui::tile_color(std::uint16_t value) noexcept
{
    switch(value) {
        case 0: return lv_color_hex(0x263638);
        case 2: return lv_color_hex(0xDCE7E1);
        case 4: return lv_color_hex(0xBCD5CB);
        case 8: return lv_color_hex(0x78B7A7);
        case 16: return lv_color_hex(0x4C9688);
        case 32: return lv_color_hex(0xD59B68);
        case 64: return lv_color_hex(0xCB7557);
        case 128: return lv_color_hex(0xD0AE5B);
        case 256: return lv_color_hex(0xC99636);
        case 512: return lv_color_hex(0xB77B2A);
        case 1024: return lv_color_hex(0x8467A6);
        default: return lv_color_hex(0x5D467D);
    }
}

lv_color_t Game2048Ui::tile_text_color(std::uint16_t value) noexcept
{
    return value <= 4 ? lv_color_hex(0x21302F) : lv_color_hex(0xFFF9ED);
}

void Game2048Ui::set_tile_value(lv_obj_t* tile, lv_obj_t* text, std::uint16_t value)
{
    lv_obj_set_style_bg_color(tile, tile_color(value), 0);
    lv_obj_set_style_text_color(text, tile_text_color(value), 0);
    if(value == 0) lv_label_set_text(text, "");
    else lv_label_set_text_fmt(text, "%u", value);
    lv_obj_set_style_text_font(
        text, value >= 1024 ? &lv_font_montserrat_16 : &lv_font_montserrat_24, 0);
}

void Game2048Ui::render(bool entering)
{
    const auto& values = game_.board();
    for(std::size_t index = 0; index < values.size(); ++index) {
        set_tile_value(tiles_[index], labels_[index], values[index]);
        if(entering && !reduced_motion_ && values[index] != 0) {
            lv_obj_set_style_transform_scale(tiles_[index], 205, 0);
            lv_anim_t animation;
            lv_anim_init(&animation);
            lv_anim_set_var(&animation, tiles_[index]);
            lv_anim_set_values(&animation, 205, 256);
            lv_anim_set_duration(&animation, 240 + static_cast<std::uint32_t>(index * 10));
            lv_anim_set_path_cb(&animation, lv_anim_path_overshoot);
            lv_anim_set_exec_cb(&animation, animate_scale);
            lv_anim_start(&animation);
        }
    }
    lv_label_set_text_fmt(score_label_, "%u", game_.score());
    lv_label_set_text_fmt(best_label_, "BEST  %u", game_.best_score());
    update_phase();
}

void Game2048Ui::request_move(MoveDirection direction)
{
    if(animating_) {
        queued_move_ = direction;
        return;
    }
    run_move(direction);
}

void Game2048Ui::run_move(MoveDirection direction)
{
    for(auto* tile : tiles_) {
        if(tile == nullptr) continue;
        lv_anim_delete(tile, animate_scale);
        lv_obj_set_style_transform_scale(tile, 256, 0);
    }
    const auto outcome = game_.move(direction);
    if(!outcome.changed) return;
    pending_outcome_ = outcome;
    if(reduced_motion_) {
        render();
        finish_move();
        return;
    }
    animating_ = true;
    begin_motion(outcome);
}

void Game2048Ui::begin_motion(const MoveOutcome& outcome)
{
    clear_motion_visuals();
    for(std::size_t index = 0; index < tiles_.size(); ++index) {
        set_tile_value(tiles_[index], labels_[index], 0);
    }
    pending_motion_animations_ = outcome.motion_count;
    for(std::size_t index = 0; index < outcome.motion_count; ++index) {
        const auto& motion = outcome.motions[index];
        auto& visual = motion_visuals_[index];
        visual.from_x = tile_x(motion.from);
        visual.from_y = tile_y(motion.from);
        visual.to_x = tile_x(motion.to);
        visual.to_y = tile_y(motion.to);
        visual.tile = lv_obj_create(board_);
        lv_obj_set_pos(visual.tile, visual.from_x, visual.from_y);
        lv_obj_set_size(visual.tile, kTileSize, kTileSize);
        lv_obj_set_style_radius(visual.tile, 11, 0);
        lv_obj_set_style_border_width(visual.tile, 1, 0);
        lv_obj_set_style_border_color(visual.tile, lv_color_hex(0x6D8A84), 0);
        lv_obj_set_style_pad_all(visual.tile, 0, 0);
        lv_obj_set_style_shadow_width(visual.tile, motion.merged ? 10 : 4, 0);
        lv_obj_set_style_shadow_color(visual.tile, tile_color(motion.value), 0);
        lv_obj_set_style_shadow_opa(
            visual.tile,
            static_cast<lv_opa_t>(motion.merged ? LV_OPA_50 : LV_OPA_20),
            0);
        lv_obj_remove_flag(visual.tile, LV_OBJ_FLAG_SCROLLABLE);
        visual.label = label(visual.tile, "", tile_text_color(motion.value));
        lv_obj_center(visual.label);
        set_tile_value(visual.tile, visual.label, motion.value);

        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, &visual);
        lv_anim_set_values(&animation, 0, 256);
        lv_anim_set_duration(&animation, 155);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&animation, animate_motion);
        lv_anim_set_user_data(&animation, this);
        lv_anim_set_completed_cb(&animation, motion_animation_complete);
        lv_anim_start(&animation);
    }
    if(pending_motion_animations_ == 0) complete_motion_phase();
}

void Game2048Ui::complete_motion_phase()
{
    clear_motion_visuals();
    render();
    start_resolution_effects();
}

void Game2048Ui::start_resolution_effects()
{
    if(!pending_outcome_) {
        finish_move();
        return;
    }
    const auto outcome = *pending_outcome_;
    if(outcome.score_delta != 0) {
        lv_label_set_text_fmt(score_fly_, "+%u", outcome.score_delta);
        lv_obj_set_style_translate_y(score_fly_, 8, 0);
        lv_anim_t fly;
        lv_anim_init(&fly);
        lv_anim_set_var(&fly, score_fly_);
        lv_anim_set_values(&fly, 8, -7);
        lv_anim_set_duration(&fly, 210);
        lv_anim_set_path_cb(&fly, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&fly, animate_translate_y);
        lv_anim_start(&fly);
    }

    std::array<bool, 16> pulsed {};
    for(std::size_t index = 0; index < outcome.motion_count; ++index) {
        const auto& motion = outcome.motions[index];
        if(!motion.merged || motion.to >= pulsed.size() || pulsed[motion.to]) continue;
        pulsed[motion.to] = true;
        lv_anim_t pulse;
        lv_anim_init(&pulse);
        lv_anim_set_var(&pulse, tiles_[motion.to]);
        lv_anim_set_values(&pulse, 256, 282);
        lv_anim_set_duration(&pulse, 85);
        lv_anim_set_reverse_duration(&pulse, 95);
        lv_anim_set_path_cb(&pulse, lv_anim_path_overshoot);
        lv_anim_set_exec_cb(&pulse, animate_scale);
        lv_anim_start(&pulse);
    }

    if(outcome.spawned_cell >= tiles_.size()) {
        finish_move();
        return;
    }
    lv_anim_t spawn;
    lv_anim_init(&spawn);
    lv_anim_set_var(&spawn, tiles_[outcome.spawned_cell]);
    lv_anim_set_values(&spawn, 112, 256);
    lv_anim_set_duration(&spawn, 190);
    lv_anim_set_path_cb(&spawn, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&spawn, animate_scale);
    lv_anim_set_user_data(&spawn, this);
    lv_anim_set_completed_cb(&spawn, resolution_animation_complete);
    lv_anim_start(&spawn);
}

void Game2048Ui::clear_motion_visuals()
{
    for(auto& visual : motion_visuals_) {
        lv_anim_delete(&visual, animate_motion);
        if(visual.tile != nullptr) lv_obj_delete(visual.tile);
        visual = {};
    }
    pending_motion_animations_ = 0;
}

void Game2048Ui::finish_move()
{
    animating_ = false;
    pending_outcome_.reset();
    if(score_fly_) {
        lv_anim_delete(score_fly_, animate_translate_y);
        lv_obj_set_style_translate_y(score_fly_, 0, 0);
    }
    lv_label_set_text(score_fly_, "");
    persist();
    if(queued_move_) {
        const auto next = *queued_move_;
        queued_move_.reset();
        run_move(next);
    }
}

void Game2048Ui::persist() noexcept
{
    if(storage_ != nullptr && storage_->available()) save_game_state(*storage_, game_);
}

void Game2048Ui::update_phase()
{
    if(game_.phase() == GamePhase::won) lv_label_set_text(status_label_, "2048  /  RESONANCE");
    else if(game_.phase() == GamePhase::lost) lv_label_set_text(status_label_, "NO MOVES");
    else lv_label_set_text(status_label_, "IN FLOW");
}

void Game2048Ui::show_restart_confirmation()
{
    if(restart_box_) return;
    restart_box_ = lv_msgbox_create(lv_layer_top());
    lv_msgbox_add_title(restart_box_, "RESET MATRIX?");
    lv_msgbox_add_text(restart_box_, "The board and score will be cleared. Best score is kept.");
    auto* cancel = lv_msgbox_add_footer_button(restart_box_, "CANCEL");
    auto* confirm = lv_msgbox_add_footer_button(restart_box_, "RESET");
    lv_obj_add_event_cb(cancel, cancel_restart_event, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(confirm, confirm_restart_event, LV_EVENT_CLICKED, this);
    lv_obj_set_width(restart_box_, 440);
}

void Game2048Ui::gesture_event(lv_event_t* event)
{
    auto* self = static_cast<Game2048Ui*>(lv_event_get_user_data(event));
    auto* input = lv_indev_active();
    if(input == nullptr || self->restart_box_ != nullptr) return;
    if(lv_event_get_code(event) == LV_EVENT_PRESSED) {
        lv_indev_get_point(input, &self->gesture_start_);
        return;
    }
    if(lv_event_get_code(event) != LV_EVENT_RELEASED) return;
    lv_point_t end {};
    lv_indev_get_point(input, &end);
    const auto dx = end.x - self->gesture_start_.x;
    const auto dy = end.y - self->gesture_start_.y;
    if(std::max(std::abs(dx), std::abs(dy)) < 28) return;
    if(std::abs(dx) > std::abs(dy))
        self->request_move(dx < 0 ? MoveDirection::left : MoveDirection::right);
    else self->request_move(dy < 0 ? MoveDirection::up : MoveDirection::down);
}

void Game2048Ui::undo_event(lv_event_t* event)
{
    auto* self = static_cast<Game2048Ui*>(lv_event_get_user_data(event));
    if(!self->animating_ && self->game_.undo()) {
        self->render();
        self->persist();
    }
}

void Game2048Ui::restart_event(lv_event_t* event)
{
    auto* self = static_cast<Game2048Ui*>(lv_event_get_user_data(event));
    if(!self->animating_) self->show_restart_confirmation();
}

void Game2048Ui::confirm_restart_event(lv_event_t* event)
{
    auto* self = static_cast<Game2048Ui*>(lv_event_get_user_data(event));
    const auto best = self->game_.best_score();
    lv_msgbox_close(self->restart_box_);
    self->restart_box_ = nullptr;
    self->game_.reset();
    self->game_.set_best_score(best);
    self->queued_move_.reset();
    self->animating_ = false;
    self->render(true);
    self->persist();
}

void Game2048Ui::cancel_restart_event(lv_event_t* event)
{
    auto* self = static_cast<Game2048Ui*>(lv_event_get_user_data(event));
    lv_msgbox_close(self->restart_box_);
    self->restart_box_ = nullptr;
}

void Game2048Ui::animate_translate_y(void* object, std::int32_t value)
{
    lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(object), value, 0);
}

void Game2048Ui::animate_scale(void* object, std::int32_t value)
{
    lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(object), value, 0);
}

void Game2048Ui::animate_motion(void* object, std::int32_t value)
{
    auto* visual = static_cast<MotionVisual*>(object);
    const auto x = visual->from_x +
                   static_cast<std::int32_t>((visual->to_x - visual->from_x) * value / 256);
    const auto y = visual->from_y +
                   static_cast<std::int32_t>((visual->to_y - visual->from_y) * value / 256);
    if(visual->tile != nullptr) lv_obj_set_pos(visual->tile, x, y);
}

void Game2048Ui::motion_animation_complete(lv_anim_t* animation)
{
    auto* self = static_cast<Game2048Ui*>(lv_anim_get_user_data(animation));
    if(self->pending_motion_animations_ == 0) return;
    --self->pending_motion_animations_;
    if(self->pending_motion_animations_ == 0) self->complete_motion_phase();
}

void Game2048Ui::resolution_animation_complete(lv_anim_t* animation)
{
    static_cast<Game2048Ui*>(lv_anim_get_user_data(animation))->finish_move();
}

}  // namespace lvgl_apps
