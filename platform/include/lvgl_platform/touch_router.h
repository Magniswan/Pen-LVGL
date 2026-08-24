#pragma once

#include "lvgl_platform/error.h"
#include "lvgl_platform/touch_protocol.h"

#include <cstddef>
#include <cstdint>

namespace lvgl_platform {

struct TouchRouteResult {
    TouchFrame frame;
    ErrorCode error {errors::input_frame_invalid};

    bool ok() const noexcept { return error.ok(); }
};

class TouchRouter {
public:
    TouchRouter(std::uint64_t session_nonce, std::int32_t width, std::int32_t height) noexcept;
    TouchRouteResult route(
        const std::uint8_t* bytes, std::size_t size, std::uint64_t now_monotonic_us) noexcept;

private:
    std::int32_t width_ {0};
    std::int32_t height_ {0};
    TouchSequenceGuard sequence_;
    TouchContactGuard contacts_;
};

}  // namespace lvgl_platform
