#pragma once

#include "lvgl_platform/package_installer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lvgl_platform {

inline constexpr const char* kApplicationPayloadRoot = "/userdisk/apps/lvgl-apps";
inline constexpr const char* kApplicationPolicyRoot = "/userdisk/apps/lvgl-app-policy";
inline constexpr const char* kApplicationInboxRoot = "/userdisk/apps/lvgl-inbox";

enum class InboxStatus : std::uint16_t {
    ready = 0,
    empty,
    invalid_token,
    candidate_not_found,
    candidate_rejected,
    root_untrusted,
    io_error,
    unsupported_platform,
};

struct InboxCandidate {
    std::string token;
    std::string app_id;
    std::string name;
    std::string version;
    std::string detail;
    std::uint64_t release_counter {0};
    std::uint32_t security_epoch {0};
    std::uint64_t package_size {0};
    bool installable {false};
};

struct InboxScanResult {
    InboxStatus status {InboxStatus::root_untrusted};
    std::string detail;
    std::vector<InboxCandidate> candidates;

    bool ok() const noexcept { return status == InboxStatus::ready || status == InboxStatus::empty; }
};

struct InboxInstallResult {
    InboxStatus status {InboxStatus::candidate_rejected};
    std::string detail;
    InstallerResult installer;

    bool ok() const noexcept { return status == InboxStatus::ready && installer.ok(); }
};

// Creates only the three fixed root-owned platform directories. No caller path
// or trust material is accepted.
InboxStatus prepare_application_storage() noexcept;
InboxScanResult scan_official_inbox(
    const InstallPolicyContext& policy_context, const CryptoProvider& crypto);
InboxInstallResult install_official_inbox_candidate(
    const std::string& token, const InstallPolicyContext& policy_context,
    const CryptoProvider& crypto);

}  // namespace lvgl_platform
