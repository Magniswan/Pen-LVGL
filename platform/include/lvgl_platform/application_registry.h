#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lvgl_platform {

inline constexpr std::size_t kMaximumRegistryApplications = 64;
inline constexpr std::size_t kMaximumRegistrySize = 64U * 1024U;

struct RegisteredApplication {
    std::string app_id;
    std::string name;
    std::string version;
    std::uint64_t release_counter {0};
    std::uint32_t security_epoch {0};
    std::uint32_t capability_count {0};
};

enum class ApplicationRegistryStatus : std::uint16_t {
    valid = 0,
    invalid_layout,
    invalid_value,
    non_canonical,
};

struct ApplicationRegistryDocument {
    ApplicationRegistryStatus status {ApplicationRegistryStatus::invalid_layout};
    std::string detail;
    std::vector<RegisteredApplication> applications;
    std::vector<std::uint8_t> bytes;

    bool ok() const noexcept { return status == ApplicationRegistryStatus::valid; }
};

ApplicationRegistryDocument encode_application_registry(
    const std::vector<RegisteredApplication>& applications);
ApplicationRegistryDocument decode_application_registry(
    const std::uint8_t* bytes, std::size_t size);

}  // namespace lvgl_platform
