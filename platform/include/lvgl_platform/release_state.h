#pragma once

#include "lvgl_platform/crypto_provider.h"
#include "lvgl_platform/package_verifier.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lvgl_platform {

enum class ReleaseStateStatus : std::uint16_t {
    valid = 0,
    invalid_layout,
    invalid_value,
    checksum_mismatch,
    crypto_unavailable,
    split_brain,
    no_valid_slot,
};

struct ApplicationReleaseState {
    std::string app_id;
    std::string signing_key_id;
    std::uint64_t generation {0};
    std::uint64_t current_release {0};
    std::uint64_t previous_release {0};
    std::uint64_t high_release {0};
    std::uint64_t quarantined_release {0};
    std::uint32_t high_security_epoch {0};
    std::uint16_t consecutive_launch_failures {0};
    Sha512Digest current_digest {};
    Sha512Digest previous_digest {};
    Sha512Digest high_digest {};
};

struct EncodedReleaseState {
    ReleaseStateStatus status {ReleaseStateStatus::invalid_value};
    std::string detail;
    std::vector<std::uint8_t> bytes;

    bool ok() const noexcept { return status == ReleaseStateStatus::valid; }
};

struct DecodedReleaseState {
    ReleaseStateStatus status {ReleaseStateStatus::invalid_layout};
    std::string detail;
    ApplicationReleaseState state;

    bool ok() const noexcept { return status == ReleaseStateStatus::valid; }
};

struct SelectedReleaseState : DecodedReleaseState {
    std::uint8_t slot {0};
    bool redundancy_degraded {false};
};

EncodedReleaseState encode_release_state(
    const ApplicationReleaseState& state, const CryptoProvider& crypto);
DecodedReleaseState decode_release_state(
    const std::uint8_t* bytes, std::size_t size, const CryptoProvider& crypto);
SelectedReleaseState select_release_state(
    const std::vector<std::uint8_t>& slot_a, const std::vector<std::uint8_t>& slot_b,
    const CryptoProvider& crypto);

std::optional<ApplicationReleaseState> activation_state_for(
    const PackageVerification& candidate, const Sha512Digest& package_digest,
    const std::optional<ApplicationReleaseState>& current = std::nullopt);
std::optional<ApplicationReleaseState> rollback_failed_release(
    const ApplicationReleaseState& current);

}  // namespace lvgl_platform
