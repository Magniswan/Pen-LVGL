#include "game_2048_persistence.h"

#include <lvgl_platform/app_storage.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

namespace lvgl_apps {
namespace {

constexpr char kMagic[] = "G2048V01";
constexpr char kRecord[] = "state.v1";

template <typename T>
void put_le(T value, std::uint8_t* output) noexcept
{
    for(std::size_t index = 0; index < sizeof(T); ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

template <typename T>
T get_le(const std::uint8_t* input) noexcept
{
    T value = 0;
    for(std::size_t index = 0; index < sizeof(T); ++index) {
        value |= static_cast<T>(input[index]) << (index * 8U);
    }
    return value;
}

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t size) noexcept
{
    std::uint32_t value = 0xffffffffU;
    for(std::size_t index = 0; index < size; ++index) {
        value ^= bytes[index];
        for(int bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -static_cast<std::int32_t>(value & 1U));
            value = (value >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~value;
}

void put_board(const std::array<std::uint16_t, 16>& board, std::uint8_t* output) noexcept
{
    for(std::size_t index = 0; index < board.size(); ++index) {
        put_le(board[index], output + index * 2U);
    }
}

void get_board(const std::uint8_t* input, std::array<std::uint16_t, 16>& board) noexcept
{
    for(std::size_t index = 0; index < board.size(); ++index) {
        board[index] = get_le<std::uint16_t>(input + index * 2U);
    }
}

}  // namespace

std::array<std::uint8_t, kGameStateWireSize> encode_game_state(
    const PersistentGameState& state) noexcept
{
    std::array<std::uint8_t, kGameStateWireSize> output {};
    std::memcpy(output.data(), kMagic, 8);
    put_le<std::uint16_t>(kGameStateWireSize, output.data() + 8);
    put_le<std::uint16_t>(1, output.data() + 10);
    put_board(state.board, output.data() + 16);
    put_le(state.score, output.data() + 48);
    put_le(state.best_score, output.data() + 52);
    put_le(state.random_state, output.data() + 56);
    output[64] = static_cast<std::uint8_t>(state.phase);
    output[65] = state.undo_valid ? 1 : 0;
    put_board(state.undo_board, output.data() + 68);
    put_le(state.undo_score, output.data() + 100);
    put_le(state.undo_random_state, output.data() + 104);
    output[112] = static_cast<std::uint8_t>(state.undo_phase);
    put_le(crc32(output.data() + 16, output.size() - 16), output.data() + 12);
    return output;
}

bool decode_game_state(
    const std::uint8_t* bytes, std::size_t size, PersistentGameState& state) noexcept
{
    if(bytes == nullptr || size != kGameStateWireSize || std::memcmp(bytes, kMagic, 8) != 0 ||
       get_le<std::uint16_t>(bytes + 8) != kGameStateWireSize ||
       get_le<std::uint16_t>(bytes + 10) != 1 ||
       get_le<std::uint32_t>(bytes + 12) != crc32(bytes + 16, size - 16) ||
       bytes[64] > static_cast<std::uint8_t>(GamePhase::lost) || bytes[65] > 1 ||
       bytes[66] != 0 || bytes[67] != 0 ||
       bytes[112] > static_cast<std::uint8_t>(GamePhase::lost) ||
       !std::all_of(bytes + 113, bytes + size, [](auto value) { return value == 0; })) {
        return false;
    }
    PersistentGameState parsed;
    get_board(bytes + 16, parsed.board);
    parsed.score = get_le<std::uint32_t>(bytes + 48);
    parsed.best_score = get_le<std::uint32_t>(bytes + 52);
    parsed.random_state = get_le<std::uint64_t>(bytes + 56);
    parsed.phase = static_cast<GamePhase>(bytes[64]);
    parsed.undo_valid = bytes[65] == 1;
    get_board(bytes + 68, parsed.undo_board);
    parsed.undo_score = get_le<std::uint32_t>(bytes + 100);
    parsed.undo_random_state = get_le<std::uint64_t>(bytes + 104);
    parsed.undo_phase = static_cast<GamePhase>(bytes[112]);
    state = parsed;
    return true;
}

bool load_game_state(dictpen::AppStorage& storage, Game2048& game) noexcept
{
    std::vector<std::uint8_t> bytes;
    PersistentGameState state;
    return storage.read(kRecord, bytes, kGameStateWireSize) &&
           decode_game_state(bytes.data(), bytes.size(), state) && game.restore_state(state);
}

bool save_game_state(dictpen::AppStorage& storage, const Game2048& game) noexcept
{
    const auto bytes = encode_game_state(game.persistent_state());
    return storage.write_atomic(kRecord, bytes.data(), bytes.size(), kGameStateWireSize);
}

}  // namespace lvgl_apps
