#pragma once

#include <string_view>

namespace dictpen {

// Capability-limited lifecycle service supplied by lvgl-sessiond.
// Application code never receives a process handle, executable path, or shell.
class AppControl {
public:
    AppControl();

    bool available() const;
    bool signal_ready() const;
    bool launch(std::string_view app_id) const;
    bool home() const;
    bool exit_session() const;

private:
    int fd_ {-1};
    int session_fd_ {-1};
};

}  // namespace dictpen
