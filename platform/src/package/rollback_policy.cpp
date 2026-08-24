#include "lvgl_platform/rollback_policy.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace lvgl_platform {
namespace {

InstallPolicyDecision decision(InstallPolicyStatus status, const char* detail)
{
    return {status, detail};
}

bool parse_version(std::string_view value, std::array<std::uint32_t, 3>& output) noexcept
{
    std::size_t start = 0;
    for(std::size_t part = 0; part < output.size(); ++part) {
        const auto end = value.find('.', start);
        if((part < 2 && end == std::string_view::npos) ||
           (part == 2 && end != std::string_view::npos)) {
            return false;
        }
        const auto component = value.substr(
            start, end == std::string_view::npos ? value.size() - start : end - start);
        if(component.empty()) return false;
        std::uint64_t number = 0;
        for(const auto character : component) {
            if(character < '0' || character > '9') return false;
            const auto digit = static_cast<std::uint32_t>(character - '0');
            if(number > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) return false;
            number = number * 10U + digit;
        }
        output[part] = static_cast<std::uint32_t>(number);
        start = end == std::string_view::npos ? value.size() : end + 1;
    }
    return start == value.size();
}

bool version_at_least(std::string_view actual, std::string_view minimum) noexcept
{
    std::array<std::uint32_t, 3> actual_parts {};
    std::array<std::uint32_t, 3> minimum_parts {};
    return parse_version(actual, actual_parts) && parse_version(minimum, minimum_parts) &&
           actual_parts >= minimum_parts;
}

bool contains(const std::vector<std::string>& values, const std::string& value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

InstallPolicyDecision evaluate_install_policy(
    const PackageVerification& candidate, const Sha512Digest& candidate_package_digest,
    const InstallPolicyContext& context,
    const std::optional<ApplicationHighWaterMark>& high_water_mark)
{
    if(!candidate.ok() || candidate.development || !candidate.signature_verified) {
        return decision(
            InstallPolicyStatus::package_not_authenticated, "POLICY_PACKAGE_NOT_AUTHENTICATED");
    }
    const auto& manifest = candidate.manifest;
    if(context.platform_version.empty() || context.sdk_abi.empty() || context.profile_id.empty() ||
       context.machine.empty() || manifest.app_id.empty() || manifest.release_counter == 0) {
        return decision(InstallPolicyStatus::invalid_policy_input, "POLICY_INPUT_INVALID");
    }
    if(manifest.sdk_abi != context.sdk_abi) {
        return decision(InstallPolicyStatus::incompatible_sdk, "POLICY_SDK_INCOMPATIBLE");
    }
    if(!version_at_least(context.platform_version, manifest.minimum_platform_version)) {
        return decision(InstallPolicyStatus::platform_too_old, "POLICY_PLATFORM_TOO_OLD");
    }
    if(!contains(manifest.supported_profiles, context.profile_id)) {
        return decision(InstallPolicyStatus::unsupported_profile, "POLICY_PROFILE_UNSUPPORTED");
    }
    if(!contains(manifest.supported_machines, context.machine)) {
        return decision(InstallPolicyStatus::unsupported_machine, "POLICY_MACHINE_UNSUPPORTED");
    }
    for(const auto& capability : manifest.capabilities) {
        if(!contains(context.allowed_capabilities, capability)) {
            return decision(InstallPolicyStatus::capability_denied, "POLICY_CAPABILITY_DENIED");
        }
    }
    if(!high_water_mark.has_value()) {
        return decision(InstallPolicyStatus::allowed, "POLICY_INSTALL_ALLOWED");
    }

    const auto& installed = *high_water_mark;
    if(installed.app_id != manifest.app_id) {
        return decision(
            InstallPolicyStatus::application_id_mismatch, "POLICY_APPLICATION_ID_MISMATCH");
    }
    if(installed.signing_key_id != manifest.signing_key_id) {
        return decision(InstallPolicyStatus::signer_changed, "POLICY_SIGNER_CHANGED");
    }
    if(manifest.security_epoch < installed.security_epoch) {
        return decision(
            InstallPolicyStatus::security_epoch_rollback, "POLICY_SECURITY_EPOCH_ROLLBACK");
    }
    if(manifest.release_counter < installed.release_counter) {
        return decision(
            InstallPolicyStatus::release_counter_rollback, "POLICY_RELEASE_COUNTER_ROLLBACK");
    }
    if(manifest.release_counter == installed.release_counter &&
       candidate_package_digest != installed.package_digest) {
        return decision(
            InstallPolicyStatus::release_counter_collision, "POLICY_RELEASE_COUNTER_COLLISION");
    }
    return decision(InstallPolicyStatus::allowed, "POLICY_INSTALL_ALLOWED");
}

ApplicationHighWaterMark advance_high_water_mark(
    const PackageVerification& candidate, const Sha512Digest& candidate_package_digest,
    const std::optional<ApplicationHighWaterMark>& current)
{
    if(current.has_value()) {
        if(candidate.manifest.app_id != current->app_id ||
           candidate.manifest.signing_key_id != current->signing_key_id ||
           candidate.manifest.release_counter <= current->release_counter ||
           candidate.manifest.security_epoch < current->security_epoch) {
            return *current;
        }
    }
    return {
        candidate.manifest.app_id, candidate.manifest.signing_key_id,
        candidate.manifest.release_counter, candidate.manifest.security_epoch,
        candidate_package_digest};
}

}  // namespace lvgl_platform
