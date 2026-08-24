#pragma once

#include "lvgl_platform/package_verifier.h"

#include <cstddef>
#include <cstdint>

namespace lvgl_platform {

class OfficialTrustStore {
public:
    static OfficialTrustStore compiled() noexcept;

    bool configured() const noexcept { return configured_; }
    const Ed25519PublicKey& public_key() const noexcept { return public_key_; }

    PackageVerification verify(
        const std::uint8_t* bytes, std::size_t size, const CryptoProvider& crypto) const;

private:
    Ed25519PublicKey public_key_ {};
    bool configured_ {false};
};

}  // namespace lvgl_platform
