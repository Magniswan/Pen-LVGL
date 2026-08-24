#include "game_2048_model.h"
#include "game_2048_persistence.h"

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
    expect(moved.motion_count == 4, "move records one motion for every source tile");
    expect(moved.motions[0].from == 0 && moved.motions[0].to == 0 &&
               moved.motions[0].value == 2 && moved.motions[0].merged,
           "first source records its merge destination");
    expect(moved.motions[1].from == 1 && moved.motions[1].to == 0 &&
               moved.motions[1].value == 2 && moved.motions[1].merged,
           "second source converges on the same merge destination");
    expect(moved.motions[2].from == 4 && moved.motions[2].to == 4 &&
               moved.motions[2].value == 4 && moved.motions[2].merged &&
               moved.motions[3].from == 6 && moved.motions[3].to == 4,
           "motions retain physical cells across rows");
    expect(game.can_undo() && game.undo(), "changed move records one undo snapshot");
    expect(game.board()[0] == 2 && game.board()[1] == 2 && game.score() == 0,
           "undo restores board and score before deterministic spawn");
    expect(!game.undo(), "undo is one-shot");

    game.set_board_for_test({2, 0, 2, 2});
    const auto right = game.move(lvgl_apps::MoveDirection::right);
    expect(right.motion_count == 3 && right.motions[0].from == 3 &&
               right.motions[0].to == 3 && right.motions[0].merged &&
               right.motions[1].from == 2 && right.motions[1].to == 3 &&
               right.motions[2].from == 0 && right.motions[2].to == 2 &&
               !right.motions[2].merged,
           "rightward motion uses screen-space source and destination cells");

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

    first.set_board_for_test({2, 2, 4, 0}, 20);
    first.set_best_score(100);
    first.move(lvgl_apps::MoveDirection::left);
    const auto encoded = lvgl_apps::encode_game_state(first.persistent_state());
    lvgl_apps::PersistentGameState decoded;
    expect(lvgl_apps::decode_game_state(encoded.data(), encoded.size(), decoded),
           "canonical persistent state decodes");
    Game2048 restored(999);
    expect(restored.restore_state(decoded) && restored.board() == first.board() &&
               restored.score() == first.score() && restored.best_score() == first.best_score() &&
               restored.can_undo(),
           "persistent state restores board, score, random state and undo");
    auto tampered = encoded;
    tampered[48] ^= 1;
    expect(!lvgl_apps::decode_game_state(tampered.data(), tampered.size(), decoded),
           "persistent state rejects corruption");
    auto invalid = first.persistent_state();
    invalid.board[0] = 3;
    expect(!restored.restore_state(invalid), "persistent state rejects non-power-of-two tiles");
    invalid = first.persistent_state();
    invalid.undo_valid = false;
    invalid.undo_board[0] = 3;
    expect(!restored.restore_state(invalid), "persistent state validates inactive undo data too");
    return 0;
}
