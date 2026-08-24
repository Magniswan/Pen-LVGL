#pragma once

#include "lvgl_platform/crypto_provider.h"
#include "lvgl_platform/package_verifier.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lvgl_platform {

enum class InstallPolicyStatus : std::uint16_t {
    allowed = 0,
    package_not_authenticated,
    incompatible_sdk,
    platform_too_old,
    unsupported_profile,
    unsupported_machine,
    capability_denied,
    application_id_mismatch,
    signer_changed,
    security_epoch_rollback,
    release_counter_rollback,
    release_counter_collision,
    invalid_policy_input,
};

struct InstallPolicyContext {
    std::string platform_version;
    std::string sdk_abi;
    std::string profile_id;
    std::string machine;
    std::vector<std::string> allowed_capabilities;
};

struct ApplicationHighWaterMark {
    std::string app_id;
    std::string signing_key_id;
    std::uint64_t release_counter {0};
    std::uint32_t security_epoch {0};
    Sha512Digest package_digest {};
};

struct InstallPolicyDecision {
    InstallPolicyStatus status {InstallPolicyStatus::invalid_policy_input};
    std::string detail;

    bool allowed() const noexcept { return status == InstallPolicyStatus::allowed; }
};

InstallPolicyDecision evaluate_install_policy(
    const PackageVerification& candidate, const Sha512Digest& candidate_package_digest,
    const InstallPolicyContext& context,
    const std::optional<ApplicationHighWaterMark>& high_water_mark = std::nullopt);

ApplicationHighWaterMark advance_high_water_mark(
    const PackageVerification& candidate, const Sha512Digest& candidate_package_digest,
    const std::optional<ApplicationHighWaterMark>& current = std::nullopt);

}  // namespace lvgl_platform
