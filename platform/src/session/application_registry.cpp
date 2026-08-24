#include "lvgl_platform/application_registry.h"

#include "lvgl_platform/session_control.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <type_traits>

namespace lvgl_platform {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic {'L', 'V', 'R', 'E', 'G', '0', '1', 0};
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kRecordSize = 24;

template <typename Integer>
void append_le(std::vector<std::uint8_t>& output, Integer value)
{
    static_assert(std::is_unsigned_v<Integer>);
    for(std::size_t index = 0; index < sizeof(Integer); ++index) {
        output.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
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

bool valid_text(std::string_view value, std::size_t maximum) noexcept
{
    if(value.empty() || value.size() > maximum) return false;
    for(const unsigned char character : value) {
        if(character == 0 || character < 0x20 || character == 0x7f) return false;
    }
    return true;
}

bool valid_version(std::string_view value) noexcept
{
    if(!valid_text(value, 32)) return false;
    unsigned int dots = 0;
    bool digit = false;
    for(const char character : value) {
        if(character == '.') {
            if(!digit || ++dots > 2) return false;
            digit = false;
        } else if(character >= '0' && character <= '9') {
            digit = true;
        } else {
            return false;
        }
    }
    return dots == 2 && digit;
}

bool valid_application(const RegisteredApplication& application) noexcept
{
    return valid_session_app_id(application.app_id) && valid_text(application.name, 64) &&
           valid_version(application.version) && application.release_counter != 0 &&
           application.capability_count <= 64;
}

ApplicationRegistryDocument failure(ApplicationRegistryStatus status, const char* detail)
{
    return {status, detail, {}, {}};
}

}  // namespace

ApplicationRegistryDocument encode_application_registry(
    const std::vector<RegisteredApplication>& applications)
{
    if(applications.empty() || applications.size() > kMaximumRegistryApplications) {
        return failure(ApplicationRegistryStatus::invalid_value, "REGISTRY_COUNT_INVALID");
    }
    std::string_view previous;
    std::size_t total = kHeaderSize;
    for(const auto& application : applications) {
        if(!valid_application(application)) {
            return failure(ApplicationRegistryStatus::invalid_value, "REGISTRY_APPLICATION_INVALID");
        }
        if(!previous.empty() && previous >= application.app_id) {
            return failure(ApplicationRegistryStatus::non_canonical, "REGISTRY_ORDER_INVALID");
        }
        previous = application.app_id;
        const auto strings = application.app_id.size() + application.name.size() +
                             application.version.size();
        if(strings > kMaximumRegistrySize - total - kRecordSize) {
            return failure(ApplicationRegistryStatus::invalid_value, "REGISTRY_SIZE_INVALID");
        }
        total += kRecordSize + strings;
    }

    ApplicationRegistryDocument result;
    result.bytes.reserve(total);
    for(const auto value : kMagic) result.bytes.push_back(value);
    append_le(result.bytes, kVersion);
    append_le(result.bytes, static_cast<std::uint16_t>(applications.size()));
    append_le(result.bytes, static_cast<std::uint32_t>(total));
    for(const auto& application : applications) {
        append_le(result.bytes, static_cast<std::uint16_t>(application.app_id.size()));
        append_le(result.bytes, static_cast<std::uint16_t>(application.name.size()));
        append_le(result.bytes, static_cast<std::uint16_t>(application.version.size()));
        append_le(result.bytes, std::uint16_t {1});
        append_le(result.bytes, application.release_counter);
        append_le(result.bytes, application.security_epoch);
        append_le(result.bytes, application.capability_count);
        for(const unsigned char value : application.app_id) result.bytes.push_back(value);
        for(const unsigned char value : application.name) result.bytes.push_back(value);
        for(const unsigned char value : application.version) result.bytes.push_back(value);
    }
    result.status = ApplicationRegistryStatus::valid;
    result.detail = "REGISTRY_VALID";
    result.applications = applications;
    return result;
}

ApplicationRegistryDocument decode_application_registry(
    const std::uint8_t* bytes, std::size_t size)
{
    if(bytes == nullptr || size < kHeaderSize || size > kMaximumRegistrySize ||
       !std::equal(kMagic.begin(), kMagic.end(), bytes) ||
       get_le<std::uint16_t>(bytes + 8) != kVersion ||
       get_le<std::uint32_t>(bytes + 12) != size) {
        return failure(ApplicationRegistryStatus::invalid_layout, "REGISTRY_LAYOUT_INVALID");
    }
    const auto count = get_le<std::uint16_t>(bytes + 10);
    if(count == 0 || count > kMaximumRegistryApplications) {
        return failure(ApplicationRegistryStatus::invalid_value, "REGISTRY_COUNT_INVALID");
    }
    ApplicationRegistryDocument result;
    result.applications.reserve(count);
    std::size_t cursor = kHeaderSize;
    for(std::size_t index = 0; index < count; ++index) {
        if(size - cursor < kRecordSize) {
            return failure(ApplicationRegistryStatus::invalid_layout, "REGISTRY_RECORD_TRUNCATED");
        }
        const auto app_size = get_le<std::uint16_t>(bytes + cursor);
        const auto name_size = get_le<std::uint16_t>(bytes + cursor + 2);
        const auto version_size = get_le<std::uint16_t>(bytes + cursor + 4);
        const auto flags = get_le<std::uint16_t>(bytes + cursor + 6);
        const auto release_counter = get_le<std::uint64_t>(bytes + cursor + 8);
        const auto security_epoch = get_le<std::uint32_t>(bytes + cursor + 16);
        const auto capability_count = get_le<std::uint32_t>(bytes + cursor + 20);
        cursor += kRecordSize;
        const std::size_t strings = static_cast<std::size_t>(app_size) + name_size + version_size;
        if(flags != 1 || strings > size - cursor) {
            return failure(ApplicationRegistryStatus::invalid_layout, "REGISTRY_RECORD_INVALID");
        }
        RegisteredApplication application;
        application.app_id.assign(reinterpret_cast<const char*>(bytes + cursor), app_size);
        cursor += app_size;
        application.name.assign(reinterpret_cast<const char*>(bytes + cursor), name_size);
        cursor += name_size;
        application.version.assign(reinterpret_cast<const char*>(bytes + cursor), version_size);
        cursor += version_size;
        application.release_counter = release_counter;
        application.security_epoch = security_epoch;
        application.capability_count = capability_count;
        if(!valid_application(application)) {
            return failure(ApplicationRegistryStatus::invalid_value, "REGISTRY_APPLICATION_INVALID");
        }
        if(!result.applications.empty() &&
           result.applications.back().app_id >= application.app_id) {
            return failure(ApplicationRegistryStatus::non_canonical, "REGISTRY_ORDER_INVALID");
        }
        result.applications.push_back(std::move(application));
    }
    if(cursor != size) {
        return failure(ApplicationRegistryStatus::invalid_layout, "REGISTRY_TRAILING_DATA");
    }
    result.status = ApplicationRegistryStatus::valid;
    result.detail = "REGISTRY_VALID";
    result.bytes.assign(bytes, bytes + size);
    return result;
}

}  // namespace lvgl_platform
