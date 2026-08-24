#include "lvgl_platform/trust_store.h"

#include "lvgl_platform/official_key_config.h"

#include <string_view>
#include <vector>

namespace lvgl_platform {
namespace {

std::uint8_t nibble(char value) noexcept
{
    if(value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if(value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    return 0xff;
}

bool decode_public_key(std::string_view hex, Ed25519PublicKey& output) noexcept
{
    if(hex.size() != output.size() * 2) return false;
    for(std::size_t index = 0; index < output.size(); ++index) {
        const auto high = nibble(hex[index * 2]);
        const auto low = nibble(hex[index * 2 + 1]);
        if(high == 0xff || low == 0xff) return false;
        output[index] = static_cast<std::uint8_t>((high << 4U) | low);
    }
    return true;
}

}  // namespace

OfficialTrustStore OfficialTrustStore::compiled() noexcept
{
    OfficialTrustStore store;
    store.configured_ = decode_public_key(kOfficialReleasePublicKeyHex, store.public_key_);
    return store;
}

PackageVerification OfficialTrustStore::verify(
    const std::uint8_t* bytes, std::size_t size, const CryptoProvider& crypto) const
{
    if(!configured_) {
        PackageVerification result;
        result.status = PackageStatus::signer_untrusted;
        result.detail = "TRUST_OFFICIAL_KEY_UNCONFIGURED";
        return result;
    }
    return verify_package(bytes, size, crypto, {{public_key_, true}});
}

}  // namespace lvgl_platform
