#include "session/session_protocol.h"

#include <cerrno>
#include <sys/socket.h>

namespace dictpen {

bool valid_session_message(const SessionMessage& message)
{
    if(message.magic != kSessionMessageMagic || message.version != kSessionProtocolVersion ||
       message.size != sizeof(SessionMessage)) {
        return false;
    }

    const auto command = static_cast<SessionCommand>(message.command);
    const auto app_id = static_cast<AppId>(message.app_id);
    switch(command) {
        case SessionCommand::launch:
            return app_id != AppId::launcher && find_app(app_id) != nullptr;
        case SessionCommand::home:
        case SessionCommand::exit_session:
            return app_id == AppId::none;
        case SessionCommand::none:
            return false;
    }
    return false;
}

bool send_session_message(int fd, SessionCommand command, AppId app_id)
{
    if(fd < 0) return false;
    SessionMessage message;
    message.command = static_cast<uint32_t>(command);
    message.app_id = static_cast<uint32_t>(app_id);
    if(!valid_session_message(message)) return false;

    ssize_t written = -1;
    do {
        written = send(fd, &message, sizeof(message), MSG_NOSIGNAL);
    } while(written < 0 && errno == EINTR);
    return written == static_cast<ssize_t>(sizeof(message));
}

}  // namespace dictpen
