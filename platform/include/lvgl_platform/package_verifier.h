#pragma once

#include "lvgl_platform/crypto_provider.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lvgl_platform {

enum class PackageStatus : std::uint16_t {
    verified = 0,
    crypto_unavailable,
    invalid_header,
    invalid_layout,
    invalid_manifest,
    invalid_path,
    invalid_entry,
    content_hash_mismatch,
    development_rejected,
    signer_untrusted,
    signature_invalid,
};

struct TrustedPublicKey {
    Ed25519PublicKey key {};
    bool production {false};
};

struct VerifiedPackageFile {
    std::string path;
    std::string role;
    std::uint16_t mode {0};
    std::uint64_t size {0};
    std::uint64_t payload_offset {0};
    Sha256Digest sha256 {};
};

struct VerifiedPackageManifest {
    std::string app_id;
    std::string name;
    std::string version;
    std::string entry;
    std::string sdk_abi;
    std::string minimum_platform_version;
    std::string signing_key_id;
    std::uint64_t release_counter {0};
    std::uint32_t security_epoch {0};
    std::vector<std::string> supported_profiles;
    std::vector<std::string> supported_machines;
    std::vector<std::string> capabilities;
    std::vector<VerifiedPackageFile> files;
};

struct PackageVerification {
    PackageStatus status {PackageStatus::invalid_header};
    std::string detail;
    bool development {false};
    bool signature_verified {false};
    std::uint64_t unsigned_size {0};
    std::uint64_t payload_start {0};
    VerifiedPackageManifest manifest;

    bool ok() const noexcept { return status == PackageStatus::verified; }
};

PackageVerification verify_package(
    const std::uint8_t* bytes, std::size_t size, const CryptoProvider& crypto,
    const std::vector<TrustedPublicKey>& trusted_keys, bool allow_development = false);

}  // namespace lvgl_platform
