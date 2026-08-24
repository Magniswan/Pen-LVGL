#include "lvgl_platform/touch_protocol.h"

#include "lvgl_platform/version.h"

#include <type_traits>

namespace lvgl_platform {
namespace {

template <typename T>
void put_le(T value, std::uint8_t* out) noexcept
{
    using Unsigned = std::make_unsigned_t<T>;
    const auto unsigned_value = static_cast<Unsigned>(value);
    for(std::size_t index = 0; index < sizeof(T); ++index) {
        out[index] = static_cast<std::uint8_t>(unsigned_value >> (index * 8U));
    }
}

template <typename T>
T get_le(const std::uint8_t* in) noexcept
{
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned value = 0;
    for(std::size_t index = 0; index < sizeof(T); ++index) {
        value |= static_cast<Unsigned>(in[index]) << (index * 8U);
    }
    return static_cast<T>(value);
}

bool valid_phase(TouchPhase phase) noexcept
{
    switch(phase) {
        case TouchPhase::start:
        case TouchPhase::move:
        case TouchPhase::end:
        case TouchPhase::cancel:
            return true;
    }
    return false;
}

}  // namespace

bool valid_touch_frame(
    const TouchFrame& frame, std::int32_t width, std::int32_t height) noexcept
{
    return frame.session_nonce != 0 && frame.sequence != 0 && frame.monotonic_us != 0 &&
           valid_phase(frame.phase) && frame.contact_id <= kMaxTouchContactId && width > 0 &&
           height > 0 && frame.x >= 0 && frame.y >= 0 && frame.x < width && frame.y < height &&
           frame.flags == 0;
}

bool touch_timestamp_fresh(
    const TouchFrame& frame, std::uint64_t now_monotonic_us,
    std::uint64_t maximum_age_us, std::uint64_t maximum_future_us) noexcept
{
    if(frame.monotonic_us == 0 || now_monotonic_us == 0) return false;
    if(frame.monotonic_us > now_monotonic_us) {
        return frame.monotonic_us - now_monotonic_us <= maximum_future_us;
    }
    return now_monotonic_us - frame.monotonic_us <= maximum_age_us;
}

TouchWireFrame encode_touch_frame(const TouchFrame& frame) noexcept
{
    TouchWireFrame bytes {};
    put_le<std::uint32_t>(kTouchFrameMagic, bytes.data());
    put_le<std::uint16_t>(kTouchProtocolVersion, bytes.data() + 4);
    put_le<std::uint16_t>(static_cast<std::uint16_t>(kTouchFrameWireSize), bytes.data() + 6);
    put_le<std::uint64_t>(frame.session_nonce, bytes.data() + 8);
    put_le<std::uint64_t>(frame.sequence, bytes.data() + 16);
    put_le<std::uint64_t>(frame.monotonic_us, bytes.data() + 24);
    put_le<std::uint32_t>(static_cast<std::uint32_t>(frame.phase), bytes.data() + 32);
    put_le<std::uint32_t>(frame.contact_id, bytes.data() + 36);
    put_le<std::int32_t>(frame.x, bytes.data() + 40);
    put_le<std::int32_t>(frame.y, bytes.data() + 44);
    put_le<std::uint32_t>(frame.flags, bytes.data() + 48);
    put_le<std::uint32_t>(0, bytes.data() + 52);
    return bytes;
}

TouchDecodeResult decode_touch_frame(
    const std::uint8_t* bytes, std::size_t size, std::int32_t width,
    std::int32_t height) noexcept
{
    TouchDecodeResult result;
    result.error = errors::input_frame_invalid;
    if(bytes == nullptr || size != kTouchFrameWireSize) return result;
    if(get_le<std::uint32_t>(bytes) != kTouchFrameMagic ||
       get_le<std::uint16_t>(bytes + 4) != kTouchProtocolVersion ||
       get_le<std::uint16_t>(bytes + 6) != kTouchFrameWireSize ||
       get_le<std::uint32_t>(bytes + 52) != 0) {
        return result;
    }

    result.frame.session_nonce = get_le<std::uint64_t>(bytes + 8);
    result.frame.sequence = get_le<std::uint64_t>(bytes + 16);
    result.frame.monotonic_us = get_le<std::uint64_t>(bytes + 24);
    result.frame.phase = static_cast<TouchPhase>(get_le<std::uint32_t>(bytes + 32));
    result.frame.contact_id = get_le<std::uint32_t>(bytes + 36);
    result.frame.x = get_le<std::int32_t>(bytes + 40);
    result.frame.y = get_le<std::int32_t>(bytes + 44);
    result.frame.flags = get_le<std::uint32_t>(bytes + 48);
    if(!valid_touch_frame(result.frame, width, height)) return result;
    result.error = errors::ok;
    return result;
}

ErrorCode TouchSequenceGuard::accept(const TouchFrame& frame) noexcept
{
    if(session_nonce_ == 0 || frame.session_nonce != session_nonce_) {
        return errors::input_session_mismatch;
    }
    if(frame.sequence <= last_sequence_) return errors::input_sequence_replayed;
    last_sequence_ = frame.sequence;
    return errors::ok;
}

void TouchSequenceGuard::reset(std::uint64_t session_nonce) noexcept
{
    session_nonce_ = session_nonce;
    last_sequence_ = 0;
}

ErrorCode TouchContactGuard::accept(const TouchFrame& frame) noexcept
{
    if(frame.contact_id > kMaxTouchContactId) return errors::input_contact_invalid;
    const auto mask = static_cast<std::uint32_t>(1U << frame.contact_id);
    const bool active = (active_contacts_ & mask) != 0;
    switch(frame.phase) {
        case TouchPhase::start:
            if(active) return errors::input_contact_invalid;
            active_contacts_ |= mask;
            return errors::ok;
        case TouchPhase::move:
            return active ? errors::ok : errors::input_contact_invalid;
        case TouchPhase::end:
        case TouchPhase::cancel:
            if(!active) return errors::input_contact_invalid;
            active_contacts_ &= ~mask;
            return errors::ok;
    }
    return errors::input_contact_invalid;
}

void TouchContactGuard::reset() noexcept
{
    active_contacts_ = 0;
}

}  // namespace lvgl_platform
