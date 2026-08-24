#pragma once

#include <cstdint>
#include <string>

namespace lvgl_platform {

enum class SessionState : std::uint8_t { starting, ready, error, stopped };

struct SessionStatusDocument {
    std::int32_t session_pid {0};
    std::uint64_t session_nonce {0};
    SessionState state {SessionState::starting};
    std::int32_t result {0};
    bool hole_ready {false};
    bool input_ready {false};
    std::int32_t logical_width {0};
    std::int32_t logical_height {0};
};

std::string encode_session_status(const SessionStatusDocument& status);

}  // namespace lvgl_platform
