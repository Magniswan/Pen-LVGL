#pragma once

#include "lvgl_platform/error.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lvgl_platform {

struct Point {
    std::int32_t x {0};
    std::int32_t y {0};

    friend constexpr bool operator==(Point lhs, Point rhs) noexcept
    {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }
};

struct Size {
    std::int32_t width {0};
    std::int32_t height {0};
};

struct Transform {
    std::uint16_t clockwise_degrees {0};
    std::int32_t offset_x {0};
    std::int32_t offset_y {0};
};

struct DeviceIdentity {
    std::string model;
    std::string firmware;
    std::string pcba;
    std::string machine;
    std::string libc;
    std::uint16_t bits {0};
};

struct DeviceProfile {
    std::string id;
    DeviceIdentity match;
    Size logical;
    Size physical;
    Transform display;
    Transform touch;
    std::string drm_device;
    std::string connector;
    std::string touch_name;
    std::string pixel_format;
    bool requires_overlay_plane {true};
    bool requires_touch_forwarding {true};
};

struct ProfileParseResult {
    DeviceProfile profile;
    ErrorCode error {errors::ok};
    std::string detail;

    bool ok() const noexcept { return error.ok(); }
};

struct ProfileSelection {
    const DeviceProfile* profile {nullptr};
    ErrorCode error {errors::profile_no_match};

    bool ok() const noexcept { return profile != nullptr && error.ok(); }
};

ProfileParseResult parse_device_profile(std::string_view json);
bool profile_matches(const DeviceProfile& profile, const DeviceIdentity& identity) noexcept;
ProfileSelection select_device_profile(
    const std::vector<DeviceProfile>& profiles, const DeviceIdentity& identity) noexcept;
Point logical_to_physical(Point logical, const DeviceProfile& profile) noexcept;
Point physical_touch_to_logical(Point physical, const DeviceProfile& profile) noexcept;

}  // namespace lvgl_platform
