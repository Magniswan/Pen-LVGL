#pragma once

#include <cstdint>

namespace dictpen {

struct Point {
    int32_t x;
    int32_t y;

    friend constexpr bool operator==(Point lhs, Point rhs)
    {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }
};

struct DeviceProfile {
    const char* id;
    const char* sku;
    const char* drm_device;
    const char* connector_name;
    const char* touch_name;
    int32_t physical_width;
    int32_t physical_height;
    int32_t logical_width;
    int32_t logical_height;
    int32_t display_cross_axis_offset;
    int32_t touch_cross_axis_offset;
};

const DeviceProfile& y01_profile();

Point logical_to_physical(Point logical, const DeviceProfile& profile);
Point raw_touch_to_logical(Point raw, const DeviceProfile& profile);

}  // namespace dictpen
