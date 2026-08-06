#include "runtime/app_control.h"

#include "session/session_protocol.h"

#include <cerrno>
#include <climits>
#include <cstdlib>

namespace dictpen {

AppControl::AppControl()
{
    const char* value = std::getenv("LVGL_APP_CONTROL_FD");
    if(!value || *value == '\0') return;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if(errno == 0 && end != value && *end == '\0' && parsed >= 0 && parsed <= INT_MAX) {
        fd_ = static_cast<int>(parsed);
    }
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
    return send_session_message(fd_, SessionCommand::exit_session, AppId::none);
}

}  // namespace dictpen
