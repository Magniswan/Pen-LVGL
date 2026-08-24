#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lvgl_platform {

inline constexpr std::size_t kStorageRecordNameSize = 64;
inline constexpr std::size_t kStorageHeaderSize = 96;
inline constexpr std::size_t kMaximumStorageRecordSize = 64U * 1024U;
inline constexpr std::size_t kMaximumStoragePacketSize =
    kStorageHeaderSize + kMaximumStorageRecordSize;

enum class StorageCommand : std::uint16_t { read = 1, write = 2 };

enum class StorageProtocolStatus : std::uint16_t {
    ok = 0,
    invalid_request,
    not_found,
    quota_exceeded,
    storage_corrupt,
    io_error,
};

struct StorageRequest {
    StorageCommand command {StorageCommand::read};
    std::uint64_t request_id {0};
    std::string record;
    std::uint32_t maximum_size {0};
    std::vector<std::uint8_t> data;
};

struct StorageResponse {
    StorageCommand command {StorageCommand::read};
    StorageProtocolStatus status {StorageProtocolStatus::invalid_request};
    std::uint64_t request_id {0};
    std::vector<std::uint8_t> data;
};

bool valid_storage_record_name(std::string_view value) noexcept;
bool storage_write_within_quota(
    std::uint32_t current_files, std::uint64_t current_bytes,
    bool record_exists, std::uint64_t existing_size, std::uint64_t new_size,
    std::uint32_t maximum_files, std::uint64_t maximum_bytes) noexcept;
std::vector<std::uint8_t> encode_storage_request(const StorageRequest& request);
bool decode_storage_request(
    const std::uint8_t* bytes, std::size_t size, StorageRequest& request);
std::vector<std::uint8_t> encode_storage_response(const StorageResponse& response);
bool decode_storage_response(
    const std::uint8_t* bytes, std::size_t size, StorageResponse& response);

}  // namespace lvgl_platform
