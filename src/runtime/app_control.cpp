#include "runtime/app_control.h"

#include "lvgl_platform/session_control.h"
#include "session/session_protocol.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sys/socket.h>

namespace dictpen {

AppControl::AppControl()
{
    const auto descriptor = [](const char* name) {
        const char* value = std::getenv(name);
        if(!value || *value == '\0') return -1;
        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value, &end, 10);
        return errno == 0 && end != value && *end == '\0' && parsed >= 3 && parsed <= INT_MAX
                   ? static_cast<int>(parsed)
                   : -1;
    };
    fd_ = descriptor("LVGL_APP_CONTROL_FD");
    session_fd_ = descriptor("LVGL_SESSION_CONTROL_FD");
}

bool AppControl::signal_ready() const
{
    if(session_fd_ < 0) return true;
    const auto bytes = lvgl_platform::encode_session_control(
        {lvgl_platform::SessionControlCommand::ready, 0});
    return send(session_fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(bytes.size());
}

bool AppControl::available() const
{
    return fd_ >= 0;
}

bool AppControl::launch(AppId app_id) const
{
    return send_session_message(fd_, SessionCommand::launch, app_id);
}

bool AppControl::home() const
{
    return send_session_message(fd_, SessionCommand::home, AppId::none);
}

bool AppControl::exit_session() const
{
    if(session_fd_ >= 0) {
        const auto bytes = lvgl_platform::encode_session_control(
            {lvgl_platform::SessionControlCommand::exit_session, 0});
        return send(session_fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL) ==
               static_cast<ssize_t>(bytes.size());
    }
    return send_session_message(fd_, SessionCommand::exit_session, AppId::none);
}

}  // namespace dictpen
