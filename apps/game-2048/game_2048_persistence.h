#pragma once

#include "game_2048_model.h"

#include <array>
#include <cstdint>

namespace dictpen { class AppStorage; }

namespace lvgl_apps {

inline constexpr std::size_t kGameStateWireSize = 128;

std::array<std::uint8_t, kGameStateWireSize> encode_game_state(
    const PersistentGameState& state) noexcept;
bool decode_game_state(
    const std::uint8_t* bytes, std::size_t size, PersistentGameState& state) noexcept;
bool load_game_state(dictpen::AppStorage& storage, Game2048& game) noexcept;
bool save_game_state(dictpen::AppStorage& storage, const Game2048& game) noexcept;

}  // namespace lvgl_apps
