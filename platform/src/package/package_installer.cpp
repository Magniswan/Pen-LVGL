#include "lvgl_platform/package_installer.h"

#include "lvgl_platform/trust_store.h"

#include <optional>
#include <utility>

namespace lvgl_platform {
namespace {

ApplicationHighWaterMark high_water_from(const ApplicationReleaseState& state)
{
    return {
        state.app_id, state.signing_key_id, state.high_release,
        state.high_security_epoch, state.high_digest};
}

}  // namespace

InstallerResult install_official_package_with_state_root(
    const std::string& payload_store_root, const std::string& state_store_root,
    const std::uint8_t* package_bytes,
    std::size_t package_size, const InstallPolicyContext& policy_context,
    const CryptoProvider& crypto)
{
    InstallerResult result;
    result.verification = OfficialTrustStore::compiled().verify(
        package_bytes, package_size, crypto);
    if(!result.verification.ok()) {
        result.status = InstallerStatus::trust_rejected;
        result.detail = result.verification.detail;
        return result;
    }

    Sha512Digest package_digest {};
    if(!crypto.sha512(package_bytes, package_size, package_digest)) {
        result.status = InstallerStatus::digest_failed;
        result.detail = "INSTALLER_DIGEST_FAILED";
        return result;
    }

    std::optional<ApplicationReleaseState> current;
    result.state_store = load_release_state(
        state_store_root, result.verification.manifest.app_id, crypto);
    if(result.state_store.status == StateStoreStatus::loaded ||
       result.state_store.status == StateStoreStatus::loaded_degraded) {
        current = result.state_store.selected.state;
    } else if(result.state_store.status != StateStoreStatus::not_found) {
        result.status = InstallerStatus::state_unavailable;
        result.detail = result.state_store.status == StateStoreStatus::uninitialized
                            ? "INSTALLER_REPAIR_REQUIRED"
                            : result.state_store.detail;
        return result;
    }

    const auto high_water = current.has_value()
                                ? std::optional<ApplicationHighWaterMark>(
                                      high_water_from(*current))
                                : std::nullopt;
    result.policy = evaluate_install_policy(
        result.verification, package_digest, policy_context, high_water);
    if(!result.policy.allowed()) {
        result.status = InstallerStatus::policy_rejected;
        result.detail = result.policy.detail;
        return result;
    }

    result.storage = stage_verified_release(
        payload_store_root, package_bytes, package_size, result.verification, package_digest);
    if(!result.storage.ok()) {
        result.status = InstallerStatus::storage_failed;
        result.detail = result.storage.detail;
        return result;
    }
    const auto next = activation_state_for(result.verification, package_digest, current);
    if(!next.has_value()) {
        result.status = InstallerStatus::transition_failed;
        result.detail = "INSTALLER_STATE_TRANSITION_REJECTED";
        return result;
    }
    result.active_state = *next;
    result.state_store = persist_release_state(state_store_root, result.active_state, crypto);
    if(!result.state_store.ok()) {
        result.status = InstallerStatus::state_commit_failed;
        result.detail = result.state_store.detail;
        return result;
    }
    if(result.state_store.status == StateStoreStatus::written_degraded) {
        result.status = InstallerStatus::installed_state_degraded;
        result.detail = "INSTALLER_INSTALLED_STATE_DEGRADED";
    } else if(result.storage.status == StorageStatus::already_present && current.has_value() &&
              current->generation == next->generation) {
        result.status = InstallerStatus::already_installed;
        result.detail = "INSTALLER_ALREADY_INSTALLED";
    } else {
        result.status = InstallerStatus::installed;
        result.detail = "INSTALLER_INSTALLED";
    }
    return result;
}

InstallerResult install_official_package(
    const std::string& store_root, const std::uint8_t* package_bytes,
    std::size_t package_size, const InstallPolicyContext& policy_context,
    const CryptoProvider& crypto)
{
    return install_official_package_with_state_root(
        store_root, store_root, package_bytes, package_size, policy_context, crypto);
}

}  // namespace lvgl_platform
