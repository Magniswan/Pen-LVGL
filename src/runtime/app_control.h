#pragma once

#include "session/app_registry.h"

#include <string_view>

namespace dictpen {

class AppControl {
public:
    AppControl();

    bool available() const;
    bool signal_ready() const;
    bool launch(AppId app_id) const;
    bool launch(std::string_view app_id) const;
    bool home() const;
    bool exit_session() const;

private:
    int fd_ {-1};
    int session_fd_ {-1};
};

}  // namespace dictpen
