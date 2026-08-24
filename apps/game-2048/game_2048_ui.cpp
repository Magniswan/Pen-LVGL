#include "game_2048_ui.h"

#include "shell/app_theme.h"

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

Game2048Ui::Game2048Ui(std::uint64_t seed) noexcept : game_(seed) {}

void Game2048Ui::create()
{
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
    auto* title = label(root, "合成矩阵", lv_color_hex(0xF2F5F1));
    lv_obj_set_pos(title, 28, 54);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    auto* hint = label(root, "滑动移动 · 相同矿石会融合", lv_color_hex(0x879895));
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

    auto* undo = action(root, "撤销", 28);
    lv_obj_add_event_cb(undo, undo_event, LV_EVENT_CLICKED, this);
    auto* restart = action(root, "重开", 166);
    lv_obj_add_event_cb(restart, restart_event, LV_EVENT_CLICKED, this);

    board_ = lv_obj_create(root);
    lv_obj_set_pos(board_, 354, 15);
    lv_obj_set_size(board_, kBoardSize, kBoardSize);
    lv_obj_set_style_radius(board_, 18, 0);
    lv_obj_set_style_bg_color(board_, lv_color_hex(0x1B292B), 0);
    lv_obj_set_style_border_width(board_, 1, 0);
    lv_obj_set_style_border_color(board_, lv_color_hex(0x45615E), 0);
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
    if(board_) lv_anim_delete(board_, animate_translate_x);
    if(board_) lv_anim_delete(board_, animate_translate_y);
    for(auto* tile : tiles_) if(tile) lv_anim_delete(tile, animate_scale);
    board_ = nullptr;
    restart_box_ = nullptr;
}

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

void Game2048Ui::render(bool entering)
{
    const auto& values = game_.board();
    for(std::size_t index = 0; index < values.size(); ++index) {
        lv_obj_set_style_bg_color(tiles_[index], tile_color(values[index]), 0);
        lv_obj_set_style_text_color(labels_[index], tile_text_color(values[index]), 0);
        if(values[index] == 0) lv_label_set_text(labels_[index], "");
        else lv_label_set_text_fmt(labels_[index], "%u", values[index]);
        lv_obj_set_style_text_font(labels_[index],
            values[index] >= 1024 ? &lv_font_montserrat_16 : &lv_font_montserrat_24, 0);
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
    const auto outcome = game_.move(direction);
    if(!outcome.changed) return;
    render();
    if(outcome.score_delta != 0) lv_label_set_text_fmt(score_fly_, "+%u", outcome.score_delta);
    if(outcome.spawned_cell < tiles_.size() && !reduced_motion_) {
        lv_anim_t spawn;
        lv_anim_init(&spawn);
        lv_anim_set_var(&spawn, tiles_[outcome.spawned_cell]);
        lv_anim_set_values(&spawn, 128, 256);
        lv_anim_set_duration(&spawn, 180);
        lv_anim_set_path_cb(&spawn, lv_anim_path_overshoot);
        lv_anim_set_exec_cb(&spawn, animate_scale);
        lv_anim_start(&spawn);
    }
    if(reduced_motion_) {
        finish_move();
        return;
    }
    animating_ = true;
    const bool horizontal = direction == MoveDirection::left || direction == MoveDirection::right;
    const auto start = direction == MoveDirection::left || direction == MoveDirection::up ? 16 : -16;
    lv_anim_t slide;
    lv_anim_init(&slide);
    lv_anim_set_var(&slide, board_);
    lv_anim_set_values(&slide, start, 0);
    lv_anim_set_duration(&slide, 150);
    lv_anim_set_path_cb(&slide, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&slide, horizontal ? animate_translate_x : animate_translate_y);
    lv_anim_set_user_data(&slide, this);
    lv_anim_set_completed_cb(&slide, move_animation_complete);
    lv_anim_start(&slide);
}

void Game2048Ui::finish_move()
{
    animating_ = false;
    lv_label_set_text(score_fly_, "");
    if(queued_move_) {
        const auto next = *queued_move_;
        queued_move_.reset();
        run_move(next);
    }
}

void Game2048Ui::update_phase()
{
    if(game_.phase() == GamePhase::won) lv_label_set_text(status_label_, "2048  /  已共振");
    else if(game_.phase() == GamePhase::lost) lv_label_set_text(status_label_, "矩阵已锁定");
    else lv_label_set_text(status_label_, "保持节奏");
}

void Game2048Ui::show_restart_confirmation()
{
    if(restart_box_) return;
    restart_box_ = lv_msgbox_create(lv_layer_top());
    lv_msgbox_add_title(restart_box_, "重新校准矩阵？");
    lv_msgbox_add_text(restart_box_, "当前棋盘和分数将被清除。最高分会保留。");
    auto* cancel = lv_msgbox_add_footer_button(restart_box_, "取消");
    auto* confirm = lv_msgbox_add_footer_button(restart_box_, "重新开始");
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
    if(!self->animating_ && self->game_.undo()) self->render();
}

void Game2048Ui::restart_event(lv_event_t* event)
{
    static_cast<Game2048Ui*>(lv_event_get_user_data(event))->show_restart_confirmation();
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
}

void Game2048Ui::cancel_restart_event(lv_event_t* event)
{
    auto* self = static_cast<Game2048Ui*>(lv_event_get_user_data(event));
    lv_msgbox_close(self->restart_box_);
    self->restart_box_ = nullptr;
}

void Game2048Ui::animate_translate_x(void* object, std::int32_t value)
{
    lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(object), value, 0);
}

void Game2048Ui::animate_translate_y(void* object, std::int32_t value)
{
    lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(object), value, 0);
}

void Game2048Ui::animate_scale(void* object, std::int32_t value)
{
    lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(object), value, 0);
}

void Game2048Ui::move_animation_complete(lv_anim_t* animation)
{
    static_cast<Game2048Ui*>(lv_anim_get_user_data(animation))->finish_move();
}

}  // namespace lvgl_apps
