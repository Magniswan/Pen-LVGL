#pragma once

#include "lvgl_platform/installer_protocol.h"

#include <cstdint>
#include <string>

namespace dictpen {

struct InstallerClientResult {
    bool success {false};
    std::string detail;
};

class InstallerClient {
public:
    InstallerClient() noexcept;
    ~InstallerClient();
    InstallerClient(const InstallerClient&) = delete;
    InstallerClient& operator=(const InstallerClient&) = delete;

    bool available() const noexcept;
    lvgl_platform::InboxScanResult scan() noexcept;
    InstallerClientResult install(const std::string& token) noexcept;

private:
    bool exchange(
        const lvgl_platform::InstallerRequest& request,
        lvgl_platform::InstallerResponse& response) noexcept;

    int descriptor_ {-1};
    std::uint64_t next_request_id_ {1};
};

}  // namespace dictpen
