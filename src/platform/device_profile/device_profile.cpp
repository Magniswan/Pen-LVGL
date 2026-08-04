#include "platform/device_profile/device_profile.h"

#include <algorithm>

namespace dictpen {
namespace {

constexpr DeviceProfile kY01Profile {
    "youdao-y01-4.8.6",
    "OVERHEAD_Y01_SKU_CHN_PRO",
    "/dev/dri/card0",
    "DSI-1",
    "hyn_ts",
    480,
    960,
    960,
    266,
    107,
    113,
};

int32_t clamp_to(int32_t value, int32_t upper_exclusive)
{
    return std::clamp(value, int32_t {0}, upper_exclusive - 1);
}

}  // namespace

const DeviceProfile& y01_profile()
{
    return kY01Profile;
}

Point logical_to_physical(Point logical, const DeviceProfile& profile)
{
    logical.x = clamp_to(logical.x, profile.logical_width);
    logical.y = clamp_to(logical.y, profile.logical_height);

    return {
        profile.display_cross_axis_offset + logical.y,
        profile.logical_width - 1 - logical.x,
    };
}

Point raw_touch_to_logical(Point raw, const DeviceProfile& profile)
{
    return {
        clamp_to(profile.physical_height - 1 - raw.y, profile.logical_width),
        clamp_to(raw.x - profile.touch_cross_axis_offset, profile.logical_height),
    };
}

}  // namespace dictpen
