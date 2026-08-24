#include "lvgl_platform/storage_protocol.h"

#include <algorithm>
#include <array>
#include <type_traits>

namespace lvgl_platform {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic {'L', 'V', 'S', 'T', 'O', 'R', '1', 0};
constexpr std::uint16_t kVersion = 1;

template <typename Integer>
void put_le(Integer value, std::uint8_t* output) noexcept
{
    static_assert(std::is_unsigned_v<Integer>);
    for(std::size_t index = 0; index < sizeof(Integer); ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

template <typename Integer>
Integer get_le(const std::uint8_t* input) noexcept
{
    static_assert(std::is_unsigned_v<Integer>);
    Integer value = 0;
    for(std::size_t index = 0; index < sizeof(Integer); ++index) {
        value |= static_cast<Integer>(input[index]) << (index * 8U);
    }
    return value;
}

bool valid_command(StorageCommand command) noexcept
{
    return command == StorageCommand::read || command == StorageCommand::write;
}

bool valid_status(StorageProtocolStatus status) noexcept
{
    return status >= StorageProtocolStatus::ok && status <= StorageProtocolStatus::io_error;
}

bool zero_padding(const std::uint8_t* bytes, std::size_t begin) noexcept
{
    return std::all_of(
        bytes + begin, bytes + kStorageHeaderSize,
        [](const auto value) { return value == 0; });
}

void encode_header(
    std::vector<std::uint8_t>& output, StorageCommand command, StorageProtocolStatus status,
    std::uint16_t name_size, std::uint32_t data_size, std::uint32_t maximum_size,
    std::uint64_t request_id) noexcept
{
    std::copy(kMagic.begin(), kMagic.end(), output.begin());
    put_le(kVersion, output.data() + 8);
    put_le(static_cast<std::uint16_t>(command), output.data() + 10);
    put_le(static_cast<std::uint16_t>(status), output.data() + 12);
    put_le(name_size, output.data() + 14);
    put_le(data_size, output.data() + 16);
    put_le(maximum_size, output.data() + 20);
    put_le(request_id, output.data() + 24);
}

bool decode_header(
    const std::uint8_t* bytes, std::size_t size, StorageCommand& command,
    StorageProtocolStatus& status, std::uint16_t& name_size, std::uint32_t& data_size,
    std::uint32_t& maximum_size, std::uint64_t& request_id) noexcept
{
    if(bytes == nullptr || size < kStorageHeaderSize || size > kMaximumStoragePacketSize ||
       !std::equal(kMagic.begin(), kMagic.end(), bytes) ||
       get_le<std::uint16_t>(bytes + 8) != kVersion) {
        return false;
    }
    command = static_cast<StorageCommand>(get_le<std::uint16_t>(bytes + 10));
    status = static_cast<StorageProtocolStatus>(get_le<std::uint16_t>(bytes + 12));
    name_size = get_le<std::uint16_t>(bytes + 14);
    data_size = get_le<std::uint32_t>(bytes + 16);
    maximum_size = get_le<std::uint32_t>(bytes + 20);
    request_id = get_le<std::uint64_t>(bytes + 24);
    return valid_command(command) && valid_status(status) &&
           data_size <= kMaximumStorageRecordSize &&
           size == kStorageHeaderSize + data_size;
}

}  // namespace

bool valid_storage_record_name(std::string_view value) noexcept
{
    if(value.empty() || value.size() > kStorageRecordNameSize || value.front() == '.' ||
       value.find("..") != std::string_view::npos) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '.' ||
               character == '_' || character == '-';
    });
}

bool storage_write_within_quota(
    std::uint32_t current_files, std::uint64_t current_bytes,
    bool record_exists, std::uint64_t existing_size, std::uint64_t new_size,
    std::uint32_t maximum_files, std::uint64_t maximum_bytes) noexcept
{
    if(maximum_files == 0 || maximum_bytes == 0 || new_size == 0 ||
       new_size > kMaximumStorageRecordSize ||
       (record_exists && (current_files == 0 || existing_size == 0 ||
                          existing_size > current_bytes)) ||
       (!record_exists && existing_size != 0)) {
        return false;
    }
    const std::uint64_t retained = current_bytes - existing_size;
    if(retained > maximum_bytes || new_size > maximum_bytes - retained) return false;
    if(record_exists) return current_files <= maximum_files;
    return current_files < maximum_files;
}

std::vector<std::uint8_t> encode_storage_request(const StorageRequest& request)
{
    const bool write = request.command == StorageCommand::write;
    if(!valid_command(request.command) || request.request_id == 0 ||
       !valid_storage_record_name(request.record) || request.maximum_size == 0 ||
       request.maximum_size > kMaximumStorageRecordSize ||
       (write && (request.data.empty() || request.data.size() > request.maximum_size)) ||
       (!write && !request.data.empty())) {
        return {};
    }
    std::vector<std::uint8_t> output(kStorageHeaderSize + request.data.size(), 0);
    encode_header(
        output, request.command, StorageProtocolStatus::ok,
        static_cast<std::uint16_t>(request.record.size()),
        static_cast<std::uint32_t>(request.data.size()), request.maximum_size,
        request.request_id);
    std::copy(request.record.begin(), request.record.end(), output.begin() + 32);
    std::copy(request.data.begin(), request.data.end(), output.begin() + kStorageHeaderSize);
    return output;
}

bool decode_storage_request(
    const std::uint8_t* bytes, std::size_t size, StorageRequest& request)
{
    StorageCommand command;
    StorageProtocolStatus status;
    std::uint16_t name_size = 0;
    std::uint32_t data_size = 0;
    std::uint32_t maximum_size = 0;
    std::uint64_t request_id = 0;
    if(!decode_header(
           bytes, size, command, status, name_size, data_size, maximum_size, request_id) ||
       status != StorageProtocolStatus::ok || request_id == 0 || name_size == 0 ||
       name_size > kStorageRecordNameSize || maximum_size == 0 ||
       maximum_size > kMaximumStorageRecordSize || data_size > maximum_size ||
       !zero_padding(bytes, 32U + name_size)) {
        return false;
    }
    const std::string record(reinterpret_cast<const char*>(bytes + 32), name_size);
    const bool write = command == StorageCommand::write;
    if(!valid_storage_record_name(record) || (write && data_size == 0) ||
       (!write && data_size != 0)) {
        return false;
    }
    request = {command, request_id, record, maximum_size,
               std::vector<std::uint8_t>(
                   bytes + kStorageHeaderSize, bytes + kStorageHeaderSize + data_size)};
    return true;
}

std::vector<std::uint8_t> encode_storage_response(const StorageResponse& response)
{
    if(!valid_command(response.command) || !valid_status(response.status) ||
       response.request_id == 0 || response.data.size() > kMaximumStorageRecordSize ||
       (response.status != StorageProtocolStatus::ok && !response.data.empty()) ||
       (response.command == StorageCommand::write && !response.data.empty())) {
        return {};
    }
    std::vector<std::uint8_t> output(kStorageHeaderSize + response.data.size(), 0);
    encode_header(
        output, response.command, response.status, 0,
        static_cast<std::uint32_t>(response.data.size()), 0, response.request_id);
    std::copy(response.data.begin(), response.data.end(), output.begin() + kStorageHeaderSize);
    return output;
}

bool decode_storage_response(
    const std::uint8_t* bytes, std::size_t size, StorageResponse& response)
{
    StorageCommand command;
    StorageProtocolStatus status;
    std::uint16_t name_size = 0;
    std::uint32_t data_size = 0;
    std::uint32_t maximum_size = 0;
    std::uint64_t request_id = 0;
    if(!decode_header(
           bytes, size, command, status, name_size, data_size, maximum_size, request_id) ||
       request_id == 0 || name_size != 0 || maximum_size != 0 ||
       !zero_padding(bytes, 32) ||
       (status != StorageProtocolStatus::ok && data_size != 0) ||
       (command == StorageCommand::write && data_size != 0)) {
        return false;
    }
    response = {command, status, request_id,
                std::vector<std::uint8_t>(
                    bytes + kStorageHeaderSize, bytes + kStorageHeaderSize + data_size)};
    return true;
}

}  // namespace lvgl_platform
