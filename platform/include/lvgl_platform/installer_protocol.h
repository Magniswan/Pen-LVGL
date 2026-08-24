#pragma once

#include "lvgl_platform/inbox_service.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lvgl_platform {

inline constexpr std::size_t kInstallerRequestSize = 160;
inline constexpr std::size_t kInstallerResponseSize = 768;

enum class InstallerCommand : std::uint16_t { scan = 1, candidate = 2, install = 3 };

enum class InstallerProtocolStatus : std::uint16_t {
    ready = 0,
    empty,
    invalid_request,
    rejected,
    not_found,
    unavailable,
    io_error,
};

struct InstallerRequest {
    InstallerCommand command {InstallerCommand::scan};
    std::uint64_t request_id {0};
    std::uint32_t index {0};
    std::string token;
};

struct InstallerResponse {
    InstallerCommand command {InstallerCommand::scan};
    InstallerProtocolStatus status {InstallerProtocolStatus::invalid_request};
    std::uint64_t request_id {0};
    std::uint32_t count {0};
    std::string detail;
    InboxCandidate candidate;
};

std::array<std::uint8_t, kInstallerRequestSize> encode_installer_request(
    const InstallerRequest& request) noexcept;
bool decode_installer_request(
    const std::uint8_t* bytes, std::size_t size, InstallerRequest& request) noexcept;

std::array<std::uint8_t, kInstallerResponseSize> encode_installer_response(
    const InstallerResponse& response) noexcept;
bool decode_installer_response(
    const std::uint8_t* bytes, std::size_t size, InstallerResponse& response) noexcept;

}  // namespace lvgl_platform
