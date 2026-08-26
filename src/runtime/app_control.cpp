#include "runtime/app_control.h"

#include "lvgl_platform/session_control.h"
#include "session/session_protocol.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sys/socket.h>

namespace dictpen {
namespace {

bool send_control(
    int descriptor, lvgl_platform::SessionControlCommand command,
    std::string_view app_id = {})
{
    if(descriptor < 0) return false;
    const auto bytes = lvgl_platform::encode_session_control(
        {command, 0, std::string(app_id)});
    return send(descriptor, bytes.data(), bytes.size(), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(bytes.size());
}

}  // namespace

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
    return send_control(session_fd_, lvgl_platform::SessionControlCommand::ready);
}

bool AppControl::available() const
{
    return fd_ >= 0 || session_fd_ >= 0;
}

bool AppControl::launch(std::string_view app_id) const
{
    return session_fd_ >= 0 && lvgl_platform::valid_session_app_id(app_id) &&
           send_control(session_fd_, lvgl_platform::SessionControlCommand::launch_application,
                        app_id);
}

bool AppControl::home() const
{
    if(session_fd_ >= 0) {
        return send_control(session_fd_, lvgl_platform::SessionControlCommand::home);
    }
    return send_session_message(fd_, SessionCommand::home, AppId::none);
}

bool AppControl::exit_session() const
{
    if(session_fd_ >= 0) {
        return send_control(session_fd_, lvgl_platform::SessionControlCommand::exit_session);
    }
    return send_session_message(fd_, SessionCommand::exit_session, AppId::none);
}

}  // namespace dictpen
