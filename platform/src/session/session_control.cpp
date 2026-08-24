#include "lvgl_platform/session_control.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace lvgl_platform {
namespace {

template <typename Integer>
void put_le(Integer value, std::uint8_t* output) noexcept
{
    using Unsigned = typename std::make_unsigned<Integer>::type;
    const auto converted = static_cast<Unsigned>(value);
    for(std::size_t index = 0; index < sizeof(Integer); ++index) {
        output[index] = static_cast<std::uint8_t>(converted >> (index * 8U));
    }
}

template <typename Integer>
Integer get_le(const std::uint8_t* input) noexcept
{
    using Unsigned = typename std::make_unsigned<Integer>::type;
    Unsigned value = 0;
    for(std::size_t index = 0; index < sizeof(Integer); ++index) {
        value |= static_cast<Unsigned>(input[index]) << (index * 8U);
    }
    return static_cast<Integer>(value);
}

}  // namespace

bool valid_session_app_id(std::string_view value) noexcept
{
    if(value.empty() || value.size() >= kSessionControlAppIdSize || value.front() == '.' ||
       value.back() == '.') {
        return false;
    }
    bool separator = false;
    bool has_dot = false;
    for(const char character : value) {
        const bool alphanumeric = (character >= 'a' && character <= 'z') ||
                                  (character >= '0' && character <= '9');
        if(character == '.' || character == '-') {
            if(separator) return false;
            separator = true;
            has_dot = has_dot || character == '.';
        } else if(alphanumeric) {
            separator = false;
        } else {
            return false;
        }
    }
    return has_dot && !separator;
}

std::array<std::uint8_t, kSessionControlSize> encode_session_control(
    const SessionControlMessage& message) noexcept
{
    std::array<std::uint8_t, kSessionControlSize> bytes {};
    put_le(kSessionControlMagic, bytes.data());
    put_le(kSessionControlVersion, bytes.data() + 4);
    put_le(static_cast<std::uint16_t>(kSessionControlSize), bytes.data() + 6);
    put_le(static_cast<std::uint32_t>(message.command), bytes.data() + 8);
    put_le(message.result, bytes.data() + 12);
    const bool launch = message.command == SessionControlCommand::launch_application;
    const auto app_id_size = launch && valid_session_app_id(message.app_id)
                                 ? static_cast<std::uint16_t>(message.app_id.size())
                                 : std::uint16_t {0};
    put_le(app_id_size, bytes.data() + 16);
    if(app_id_size != 0) {
        std::copy_n(message.app_id.begin(), app_id_size, bytes.begin() + 20);
    }
    return bytes;
}

bool decode_session_control(
    const std::uint8_t* bytes, std::size_t size, SessionControlMessage& output) noexcept
{
    if(bytes == nullptr || size != kSessionControlSize ||
       get_le<std::uint32_t>(bytes) != kSessionControlMagic ||
       get_le<std::uint16_t>(bytes + 4) != kSessionControlVersion ||
       get_le<std::uint16_t>(bytes + 6) != kSessionControlSize) {
        return false;
    }
    const auto command = static_cast<SessionControlCommand>(get_le<std::uint32_t>(bytes + 8));
    const auto result = get_le<std::uint32_t>(bytes + 12);
    const auto app_id_size = get_le<std::uint16_t>(bytes + 16);
    if(get_le<std::uint16_t>(bytes + 18) != 0 || app_id_size >= kSessionControlAppIdSize ||
       std::any_of(bytes + 20 + app_id_size, bytes + size,
                   [](std::uint8_t value) { return value != 0; }) ||
       (command != SessionControlCommand::ready &&
        command != SessionControlCommand::exit_session &&
        command != SessionControlCommand::launch_application &&
        command != SessionControlCommand::home) || result != 0) {
        return false;
    }
    std::string app_id(
        reinterpret_cast<const char*>(bytes + 20), static_cast<std::size_t>(app_id_size));
    if((command == SessionControlCommand::launch_application) != !app_id.empty() ||
       (!app_id.empty() && !valid_session_app_id(app_id))) {
        return false;
    }
    output = {command, result, std::move(app_id)};
    return true;
}

}  // namespace lvgl_platform
