#pragma once

#include "lvgl_platform/installer_protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dictpen {

struct InstallerClientResult {
    bool success {false};
    std::string detail;
};

struct InstalledApplicationScanResult {
    bool success {false};
    std::string detail;
    std::vector<lvgl_platform::InstalledApplicationCandidate> applications;
};

class InstallerClient {
public:
    InstallerClient() noexcept;
    ~InstallerClient();
    InstallerClient(const InstallerClient&) = delete;
    InstallerClient& operator=(const InstallerClient&) = delete;

    bool available() const noexcept;
    lvgl_platform::InboxScanResult scan() noexcept;
    InstalledApplicationScanResult scan_installed() noexcept;
    InstallerClientResult install(const std::string& token) noexcept;
    InstallerClientResult rollback(const std::string& token) noexcept;
    InstallerClientResult remove(const std::string& token) noexcept;

private:
    bool exchange(
        const lvgl_platform::InstallerRequest& request,
        lvgl_platform::InstallerResponse& response) noexcept;
    InstallerClientResult mutate(
        lvgl_platform::InstallerCommand command, const std::string& token) noexcept;

    int descriptor_ {-1};
    std::uint64_t next_request_id_ {1};
};

}  // namespace dictpen
