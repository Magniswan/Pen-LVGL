#include "lvgl_platform/session_status.h"

#include <string_view>

namespace lvgl_platform {
namespace {

std::string_view stateName(SessionState state) noexcept
{
    switch(state) {
        case SessionState::starting: return "starting";
        case SessionState::ready: return "ready";
        case SessionState::error: return "error";
        case SessionState::stopped: return "stopped";
    }
    return "error";
}

}  // namespace

std::string encode_session_status(const SessionStatusDocument& status)
{
    if(status.session_pid <= 1 || status.session_nonce == 0 || status.result < 0 ||
       status.logical_width <= 0 || status.logical_height <= 0 ||
       (status.state != SessionState::ready && (status.hole_ready || status.input_ready)) ||
       (status.input_ready && !status.hole_ready)) {
        return {};
    }
    std::string output;
    output.reserve(192);
    output += "SESSION_PID=" + std::to_string(status.session_pid) + '\n';
    output += "SESSION_NONCE=" + std::to_string(status.session_nonce) + '\n';
    output += "STATE=";
    output += stateName(status.state);
    output += '\n';
    output += "RESULT=" + std::to_string(status.result) + '\n';
    output += std::string("HOLE_READY=") + (status.hole_ready ? "1\n" : "0\n");
    output += std::string("INPUT_READY=") + (status.input_ready ? "1\n" : "0\n");
    output += "LOGICAL_WIDTH=" + std::to_string(status.logical_width) + '\n';
    output += "LOGICAL_HEIGHT=" + std::to_string(status.logical_height) + '\n';
    return output;
}

}  // namespace lvgl_platform
