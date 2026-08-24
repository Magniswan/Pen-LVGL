#include "lvgl_platform/touch_router.h"

namespace lvgl_platform {

TouchRouter::TouchRouter(
    std::uint64_t session_nonce, std::int32_t width, std::int32_t height) noexcept
    : width_(width), height_(height)
{
    sequence_.reset(session_nonce);
    contacts_.reset();
}

TouchRouteResult TouchRouter::route(
    const std::uint8_t* bytes, std::size_t size, std::uint64_t now_monotonic_us) noexcept
{
    TouchRouteResult result;
    const auto decoded = decode_touch_frame(bytes, size, width_, height_);
    result.frame = decoded.frame;
    if(!decoded.ok()) {
        result.error = decoded.error;
        return result;
    }
    if(!touch_timestamp_fresh(decoded.frame, now_monotonic_us)) {
        result.error = errors::input_timestamp_stale;
        return result;
    }
    result.error = sequence_.accept(decoded.frame);
    if(!result.error.ok()) return result;
    result.error = contacts_.accept(decoded.frame);
    return result;
}

}  // namespace lvgl_platform
