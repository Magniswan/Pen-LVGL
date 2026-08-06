#pragma once

#include "session/app_registry.h"

#include <cstdint>

namespace dictpen {

constexpr uint32_t kSessionMessageMagic = 0x4C564150U;
constexpr uint16_t kSessionProtocolVersion = 1;

enum class SessionCommand : uint32_t {
    none = 0,
    launch = 1,
    home = 2,
    exit_session = 3,
};

struct SessionMessage {
    uint32_t magic {kSessionMessageMagic};
    uint16_t version {kSessionProtocolVersion};
    uint16_t size {sizeof(SessionMessage)};
    uint32_t command {static_cast<uint32_t>(SessionCommand::none)};
    uint32_t app_id {static_cast<uint32_t>(AppId::none)};
};

bool valid_session_message(const SessionMessage& message);
bool send_session_message(int fd, SessionCommand command, AppId app_id);

}  // namespace dictpen
