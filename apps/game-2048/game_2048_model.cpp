#include "game_2048_model.h"

#include <algorithm>
#include <limits>

namespace lvgl_apps {

Game2048::Game2048(std::uint64_t seed) noexcept : random_state_(seed == 0 ? 1 : seed)
{
    reset();
}

LineMove Game2048::collapse_line(const std::array<std::uint16_t, 4>& input) noexcept
{
    LineMove result;
    std::array<std::uint16_t, 4> compact {};
    std::size_t count = 0;
    for(const auto value : input) {
        if(value != 0) compact[count++] = value;
    }
    std::size_t output = 0;
    for(std::size_t index = 0; index < count; ++index) {
        auto value = compact[index];
        if(index + 1 < count && compact[index + 1] == value) {
            if(value <= std::numeric_limits<std::uint16_t>::max() / 2U) value *= 2U;
            result.score_delta += value;
            result.merge_mask |= static_cast<std::uint8_t>(1U << output);
            ++index;
        }
        result.values[output++] = value;
    }
    result.changed = result.values != input;
    return result;
}

std::uint8_t Game2048::cell(
    MoveDirection direction, std::uint8_t line, std::uint8_t offset) noexcept
{
    switch(direction) {
        case MoveDirection::left: return static_cast<std::uint8_t>(line * 4U + offset);
        case MoveDirection::right: return static_cast<std::uint8_t>(line * 4U + (3U - offset));
        case MoveDirection::up: return static_cast<std::uint8_t>(offset * 4U + line);
        case MoveDirection::down: return static_cast<std::uint8_t>((3U - offset) * 4U + line);
    }
    return 0;
}

std::uint64_t Game2048::random() noexcept
{
    auto value = random_state_;
    value ^= value >> 12U;
    value ^= value << 25U;
    value ^= value >> 27U;
    random_state_ = value;
    return value * 2685821657736338717ULL;
}

bool Game2048::spawn(MoveOutcome* outcome) noexcept
{
    std::array<std::uint8_t, 16> empty {};
    std::size_t count = 0;
    for(std::uint8_t index = 0; index < board_.size(); ++index) {
        if(board_[index] == 0) empty[count++] = index;
    }
    if(count == 0) return false;
    const auto selected = empty[static_cast<std::size_t>(random() % count)];
    const auto value = random() % 10U == 0 ? std::uint16_t {4} : std::uint16_t {2};
    board_[selected] = value;
    if(outcome != nullptr) {
        outcome->spawned_cell = selected;
        outcome->spawned_value = value;
    }
    return true;
}

void Game2048::reset() noexcept
{
    board_.fill(0);
    score_ = 0;
    phase_ = GamePhase::playing;
    undo_valid_ = false;
    spawn(nullptr);
    spawn(nullptr);
}

MoveOutcome Game2048::move(MoveDirection direction) noexcept
{
    MoveOutcome outcome;
    if(phase_ == GamePhase::lost) return outcome;
    const Snapshot before {board_, score_, phase_, random_state_};
    auto next = board_;
    for(std::uint8_t line = 0; line < 4; ++line) {
        std::array<std::uint16_t, 4> values {};
        std::array<std::uint8_t, 4> sources {};
        std::size_t source_count = 0;
        for(std::uint8_t offset = 0; offset < 4; ++offset) {
            values[offset] = board_[cell(direction, line, offset)];
            if(values[offset] != 0) sources[source_count++] = offset;
        }
        const auto collapsed = collapse_line(values);
        outcome.score_delta += collapsed.score_delta;
        for(std::uint8_t offset = 0; offset < 4; ++offset) {
            next[cell(direction, line, offset)] = collapsed.values[offset];
        }

        std::size_t source = 0;
        std::uint8_t destination = 0;
        while(source < source_count) {
            const auto first_offset = sources[source];
            const auto first_value = values[first_offset];
            const bool merged = source + 1 < source_count &&
                                values[sources[source + 1]] == first_value;
            outcome.motions[outcome.motion_count++] = {
                cell(direction, line, first_offset), cell(direction, line, destination),
                first_value, merged};
            if(merged) {
                const auto second_offset = sources[source + 1];
                outcome.motions[outcome.motion_count++] = {
                    cell(direction, line, second_offset), cell(direction, line, destination),
                    first_value, true};
                source += 2;
            } else {
                ++source;
            }
            ++destination;
        }
    }
    if(next == board_) {
        outcome.motion_count = 0;
        update_phase();
        return outcome;
    }
    board_ = next;
    outcome.changed = true;
    score_ += outcome.score_delta;
    best_score_ = std::max(best_score_, score_);
    undo_ = before;
    undo_valid_ = true;
    spawn(&outcome);
    update_phase();
    return outcome;
}

bool Game2048::undo() noexcept
{
    if(!undo_valid_) return false;
    board_ = undo_.board;
    score_ = undo_.score;
    phase_ = undo_.phase;
    random_state_ = undo_.random_state;
    undo_valid_ = false;
    return true;
}

GamePhase Game2048::phase_for_board(const std::array<std::uint16_t, 16>& board) noexcept
{
    if(std::find_if(board.begin(), board.end(), [](auto value) { return value >= 2048; }) !=
       board.end()) {
        return GamePhase::won;
    }
    if(std::find(board.begin(), board.end(), 0) != board.end()) return GamePhase::playing;
    for(std::uint8_t row = 0; row < 4; ++row) {
        for(std::uint8_t column = 0; column < 4; ++column) {
            const auto index = static_cast<std::uint8_t>(row * 4U + column);
            if((column < 3 && board[index] == board[index + 1]) ||
               (row < 3 && board[index] == board[index + 4])) {
                return GamePhase::playing;
            }
        }
    }
    return GamePhase::lost;
}

bool Game2048::valid_board(const std::array<std::uint16_t, 16>& board) noexcept
{
    return std::all_of(board.begin(), board.end(), [](const auto value) {
        return value == 0 || (value >= 2 && (value & (value - 1U)) == 0);
    });
}

void Game2048::update_phase() noexcept
{
    phase_ = phase_for_board(board_);
}

PersistentGameState Game2048::persistent_state() const noexcept
{
    return {board_, score_, best_score_, phase_, random_state_, undo_.board, undo_.score,
            undo_.phase, undo_.random_state, undo_valid_};
}

bool Game2048::restore_state(const PersistentGameState& state) noexcept
{
    if(!valid_board(state.board) || state.random_state == 0 || state.best_score < state.score ||
       phase_for_board(state.board) != state.phase || !valid_board(state.undo_board) ||
       state.undo_random_state == 0 || state.undo_score > state.best_score ||
       phase_for_board(state.undo_board) != state.undo_phase) {
        return false;
    }
    board_ = state.board;
    score_ = state.score;
    best_score_ = state.best_score;
    phase_ = state.phase;
    random_state_ = state.random_state;
    undo_ = {state.undo_board, state.undo_score, state.undo_phase, state.undo_random_state};
    undo_valid_ = state.undo_valid;
    return true;
}

void Game2048::set_best_score(std::uint32_t value) noexcept
{
    best_score_ = std::max(value, score_);
}

void Game2048::set_board_for_test(
    const std::array<std::uint16_t, 16>& board, std::uint32_t score) noexcept
{
    board_ = board;
    score_ = score;
    best_score_ = std::max(best_score_, score_);
    undo_valid_ = false;
    update_phase();
}

}  // namespace lvgl_apps
