#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lvgl_apps {

enum class MoveDirection : std::uint8_t { left, right, up, down };
enum class GamePhase : std::uint8_t { playing, won, lost };

struct LineMove {
    std::array<std::uint16_t, 4> values {};
    std::uint32_t score_delta {0};
    std::uint8_t merge_mask {0};
    bool changed {false};
};

struct TileMotion {
    std::uint8_t from {0};
    std::uint8_t to {0};
    std::uint16_t value {0};
    bool merged {false};
};

struct MoveOutcome {
    bool changed {false};
    std::uint32_t score_delta {0};
    std::array<TileMotion, 16> motions {};
    std::size_t motion_count {0};
    std::uint8_t spawned_cell {255};
    std::uint16_t spawned_value {0};
};

class Game2048 {
public:
    explicit Game2048(std::uint64_t seed = 1) noexcept;

    void reset() noexcept;
    MoveOutcome move(MoveDirection direction) noexcept;
    bool undo() noexcept;

    const std::array<std::uint16_t, 16>& board() const noexcept { return board_; }
    std::uint32_t score() const noexcept { return score_; }
    std::uint32_t best_score() const noexcept { return best_score_; }
    GamePhase phase() const noexcept { return phase_; }
    bool can_undo() const noexcept { return undo_valid_; }

    void set_best_score(std::uint32_t value) noexcept;
    void set_board_for_test(
        const std::array<std::uint16_t, 16>& board, std::uint32_t score = 0) noexcept;

    static LineMove collapse_line(const std::array<std::uint16_t, 4>& input) noexcept;

private:
    struct Snapshot {
        std::array<std::uint16_t, 16> board {};
        std::uint32_t score {0};
        GamePhase phase {GamePhase::playing};
        std::uint64_t random_state {1};
    };

    std::uint64_t random() noexcept;
    bool spawn(MoveOutcome* outcome) noexcept;
    void update_phase() noexcept;
    static std::uint8_t cell(MoveDirection direction, std::uint8_t line, std::uint8_t offset) noexcept;

    std::array<std::uint16_t, 16> board_ {};
    std::uint32_t score_ {0};
    std::uint32_t best_score_ {0};
    GamePhase phase_ {GamePhase::playing};
    std::uint64_t random_state_ {1};
    Snapshot undo_ {};
    bool undo_valid_ {false};
};

}  // namespace lvgl_apps
