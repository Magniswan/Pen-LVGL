#include "lvgl_platform/crypto_provider.h"
#include "lvgl_platform/release_state.h"

#include <algorithm>
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

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    (void)lvgl_platform::decode_release_state(data, size, crypto());
    if(size >= 2) {
        const std::size_t payload_size = size - 2;
        const std::size_t selector =
            (static_cast<std::size_t>(data[0]) << 8U) | data[1];
        const std::size_t split = payload_size == 0 ? 0 : selector % (payload_size + 1);
        const std::vector<std::uint8_t> slot_a(data + 2, data + 2 + split);
        const std::vector<std::uint8_t> slot_b(data + 2 + split, data + size);
        (void)lvgl_platform::select_release_state(slot_a, slot_b, crypto());
    }
    return 0;
}
