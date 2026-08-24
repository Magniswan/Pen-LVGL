#pragma once

#include "lvgl_platform/package_verifier.h"
#include "lvgl_platform/release_state.h"
#include "lvgl_platform/rollback_policy.h"
#include "lvgl_platform/safe_path.h"
#include "lvgl_platform/state_store.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace lvgl_platform {

enum class InstallerStatus : std::uint16_t {
    installed = 0,
    already_installed,
    installed_state_degraded,
    trust_rejected,
    digest_failed,
    state_unavailable,
    policy_rejected,
    storage_failed,
    transition_failed,
    state_commit_failed,
};

struct InstallerResult {
    InstallerStatus status {InstallerStatus::trust_rejected};
    std::string detail;
    PackageVerification verification;
    InstallPolicyDecision policy;
    StorageResult storage;
    StateStoreResult state_store;
    ApplicationReleaseState active_state;

    bool ok() const noexcept
    {
        return status == InstallerStatus::installed ||
               status == InstallerStatus::already_installed ||
               status == InstallerStatus::installed_state_degraded;
    }
};

// Production entry point. The only trust source is the public key compiled into
// OfficialTrustStore; callers cannot inject or import another key.
InstallerResult install_official_package(
    const std::string& store_root, const std::uint8_t* package_bytes,
    std::size_t package_size, const InstallPolicyContext& policy_context,
    const CryptoProvider& crypto);

// Variant for removable payloads. Anti-rollback state remains in a separate,
// trusted root when the payload store is intentionally removed.
InstallerResult install_official_package_with_state_root(
    const std::string& payload_store_root, const std::string& state_store_root,
    const std::uint8_t* package_bytes, std::size_t package_size,
    const InstallPolicyContext& policy_context, const CryptoProvider& crypto);

}  // namespace lvgl_platform
