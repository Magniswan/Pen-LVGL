#include "lvgl_platform/session_control.h"

#include <type_traits>

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

std::array<std::uint8_t, kSessionControlSize> encode_session_control(
    const SessionControlMessage& message) noexcept
{
    std::array<std::uint8_t, kSessionControlSize> bytes {};
    put_le(kSessionControlMagic, bytes.data());
    put_le(kSessionControlVersion, bytes.data() + 4);
    put_le(static_cast<std::uint16_t>(kSessionControlSize), bytes.data() + 6);
    put_le(static_cast<std::uint32_t>(message.command), bytes.data() + 8);
    put_le(message.result, bytes.data() + 12);
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
    if((command != SessionControlCommand::ready && command != SessionControlCommand::exit_session) ||
       (command == SessionControlCommand::ready && result != 0)) {
        return false;
    }
    output = {command, result};
    return true;
}

}  // namespace lvgl_platform
