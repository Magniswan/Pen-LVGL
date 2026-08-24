#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lvgl_platform {

inline constexpr std::uint32_t kSessionControlMagic = 0x4c565343U;
inline constexpr std::uint16_t kSessionControlVersion = 1;
inline constexpr std::size_t kSessionControlSize = 16;

enum class SessionControlCommand : std::uint32_t { ready = 1, exit_session = 2 };

struct SessionControlMessage {
    SessionControlCommand command {SessionControlCommand::ready};
    std::uint32_t result {0};
};

std::array<std::uint8_t, kSessionControlSize> encode_session_control(
    const SessionControlMessage& message) noexcept;
bool decode_session_control(
    const std::uint8_t* bytes, std::size_t size, SessionControlMessage& output) noexcept;

}  // namespace lvgl_platform
