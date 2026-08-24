#pragma once

#include "lvgl_platform/crypto_provider.h"
#include "lvgl_platform/package_verifier.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace lvgl_platform {

enum class StorageStatus : std::uint16_t {
    committed = 0,
    already_present,
    invalid_argument,
    package_not_authenticated,
    root_untrusted,
    path_rejected,
    release_collision,
    random_unavailable,
    io_error,
    unsupported_platform,
};

struct StorageResult {
    StorageStatus status {StorageStatus::invalid_argument};
    std::string detail;
    std::string release_path;

    bool ok() const noexcept
    {
        return status == StorageStatus::committed || status == StorageStatus::already_present;
    }
};

// The store root must already exist, be owned by the effective user, and not be
// writable by group or other. The function never follows links below that root.
StorageResult stage_verified_release(
    const std::string& store_root, const std::uint8_t* package_bytes,
    std::size_t package_size, const PackageVerification& verification,
    const Sha512Digest& package_digest);

}  // namespace lvgl_platform
