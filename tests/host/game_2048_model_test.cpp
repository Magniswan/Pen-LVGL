#include "game_2048_model.h"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message)
{
    if(condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

}  // namespace

int main()
{
    using lvgl_apps::Game2048;
    const auto simple = Game2048::collapse_line({2, 0, 2, 2});
    expect(simple.values == std::array<std::uint16_t, 4> {4, 2, 0, 0} &&
               simple.score_delta == 4 && simple.merge_mask == 1,
           "compresses and merges exactly once");
    const auto chain = Game2048::collapse_line({2, 2, 4, 4});
    expect(chain.values == std::array<std::uint16_t, 4> {4, 8, 0, 0} &&
               chain.score_delta == 12,
           "does not merge a newly created tile twice");
    const auto four = Game2048::collapse_line({2, 2, 2, 2});
    expect(four.values == std::array<std::uint16_t, 4> {4, 4, 0, 0},
           "pairs four equal tiles independently");

    Game2048 game(0x12345678);
    game.set_board_for_test({
        2, 2, 0, 0,
        4, 0, 4, 0,
        0, 0, 0, 0,
        0, 0, 0, 0});
    const auto moved = game.move(lvgl_apps::MoveDirection::left);
    expect(moved.changed && moved.score_delta == 12 && game.score() == 12,
           "move combines all rows and scores them");
    expect(game.can_undo() && game.undo(), "changed move records one undo snapshot");
    expect(game.board()[0] == 2 && game.board()[1] == 2 && game.score() == 0,
           "undo restores board and score before deterministic spawn");
    expect(!game.undo(), "undo is one-shot");

    game.set_board_for_test({
        2, 4, 2, 4,
        4, 2, 4, 2,
        2, 4, 2, 4,
        4, 2, 4, 2});
    expect(game.phase() == lvgl_apps::GamePhase::lost, "detects a board with no legal move");
    expect(!game.move(lvgl_apps::MoveDirection::left).changed,
           "lost board cannot mutate");

    game.set_board_for_test({2048, 0, 0, 0});
    expect(game.phase() == lvgl_apps::GamePhase::won, "detects the 2048 victory tile");

    Game2048 first(77);
    Game2048 second(77);
    expect(first.board() == second.board(), "equal seeds produce equal initial games");
    return 0;
}
