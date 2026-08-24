#pragma once

#include "lvgl_platform/error.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace lvgl_platform {

inline constexpr std::uint32_t kTouchFrameMagic = 0x4C565450U;
inline constexpr std::size_t kTouchFrameWireSize = 56;
inline constexpr std::uint32_t kMaxTouchContactId = 31;

enum class TouchPhase : std::uint32_t {
    start = 1,
    move = 2,
    end = 3,
    cancel = 4,
};

struct TouchFrame {
    std::uint64_t session_nonce {0};
    std::uint64_t sequence {0};
    std::uint64_t monotonic_us {0};
    TouchPhase phase {TouchPhase::cancel};
    std::uint32_t contact_id {0};
    std::int32_t x {0};
    std::int32_t y {0};
    std::uint32_t flags {0};
};

struct TouchDecodeResult {
    TouchFrame frame;
    ErrorCode error {errors::ok};

    bool ok() const noexcept { return error.ok(); }
};

using TouchWireFrame = std::array<std::uint8_t, kTouchFrameWireSize>;

bool valid_touch_frame(const TouchFrame& frame, std::int32_t width, std::int32_t height) noexcept;
TouchWireFrame encode_touch_frame(const TouchFrame& frame) noexcept;
TouchDecodeResult decode_touch_frame(
    const std::uint8_t* bytes, std::size_t size, std::int32_t width,
    std::int32_t height) noexcept;

class TouchSequenceGuard {
public:
    ErrorCode accept(const TouchFrame& frame) noexcept;
    void reset(std::uint64_t session_nonce) noexcept;

private:
    std::uint64_t session_nonce_ {0};
    std::uint64_t last_sequence_ {0};
};

}  // namespace lvgl_platform
