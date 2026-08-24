#pragma once

#include "lvgl_platform/crypto_provider.h"
#include "lvgl_platform/release_state.h"

#include <cstdint>
#include <string>

namespace lvgl_platform {

enum class StateStoreStatus : std::uint16_t {
    written = 0,
    written_degraded,
    loaded,
    loaded_degraded,
    not_found,
    uninitialized,
    invalid_argument,
    root_untrusted,
    path_rejected,
    state_invalid,
    io_error,
    random_unavailable,
    unsupported_platform,
};

struct StateStoreResult {
    StateStoreStatus status {StateStoreStatus::invalid_argument};
    std::string detail;
    SelectedReleaseState selected;

    bool ok() const noexcept
    {
        return status == StateStoreStatus::written ||
               status == StateStoreStatus::written_degraded ||
               status == StateStoreStatus::loaded ||
               status == StateStoreStatus::loaded_degraded;
    }
};

StateStoreResult persist_release_state(
    const std::string& store_root, const ApplicationReleaseState& state,
    const CryptoProvider& crypto);
StateStoreResult load_release_state(
    const std::string& store_root, const std::string& app_id,
    const CryptoProvider& crypto);

}  // namespace lvgl_platform
