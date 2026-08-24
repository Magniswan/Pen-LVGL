#include "lvgl_platform/crypto_provider.h"
#include "lvgl_platform/package_verifier.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

const lvgl_platform::CryptoProvider& crypto()
{
    static const std::unique_ptr<lvgl_platform::CryptoProvider> provider =
        lvgl_platform::CryptoProvider::load_default();
    if(provider == nullptr) std::abort();
    return *provider;
}

const std::vector<lvgl_platform::TrustedPublicKey>& test_keys()
{
    static const std::vector<lvgl_platform::TrustedPublicKey> keys {{
        std::array<std::uint8_t, 32> {
            0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
            0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
            0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
            0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a,
        },
        true,
    }};
    return keys;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    (void)lvgl_platform::verify_package(data, size, crypto(), test_keys(), true);
    return 0;
}
