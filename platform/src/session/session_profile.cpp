#include "lvgl_platform/session_profile.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <map>
#include <string>

namespace lvgl_platform {
namespace {

constexpr std::array<std::string_view, 17> kKeys {
    "PROFILE_ID", "MACHINE", "LOGICAL_WIDTH", "LOGICAL_HEIGHT", "DRM_DEVICE",
    "DRM_CONNECTOR_ID", "DRM_CRTC_ID", "DRM_OVERLAY_PLANE_ID", "DRM_OVERLAY_ZPOS",
    "DISPLAY_X", "DISPLAY_Y", "DISPLAY_WIDTH", "DISPLAY_HEIGHT", "PIXEL_FORMAT",
    "DISPLAY_ROTATION", "HOLE_SESSION_CERTIFIED", "PERSONAL_UNBOUND",
};

bool valid_token(std::string_view value, std::size_t limit) noexcept
{
    if(value.empty() || value.size() > limit) return false;
    return std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '.' ||
               character == '-' || character == '_' || character == '+';
    });
}

template <typename Integer>
bool parse_integer(std::string_view value, Integer minimum, Integer maximum, Integer& output) noexcept
{
    if(value.empty() || value.size() > 10) return false;
    Integer parsed {};
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    if(conversion.ec != std::errc() || conversion.ptr != value.data() + value.size() ||
       parsed < minimum || parsed > maximum) {
        return false;
    }
    output = parsed;
    return true;
}

bool parse_unsigned(
    std::string_view value, std::uint32_t minimum, std::uint32_t maximum,
    std::uint32_t& output) noexcept
{
    return parse_integer(value, minimum, maximum, output);
}

bool parse_signed(
    std::string_view value, std::int32_t minimum, std::int32_t maximum,
    std::int32_t& output) noexcept
{
    return parse_integer(value, minimum, maximum, output);
}

}  // namespace

SessionProfileParseResult parse_session_profile(std::string_view contents)
{
    SessionProfileParseResult result;
    if(contents.empty() || contents.size() > 4096 || contents.back() != '\n' ||
       contents.find('\r') != std::string_view::npos ||
       contents.find('\0') != std::string_view::npos) {
        result.detail = "SESSION_PROFILE_ENCODING_INVALID";
        return result;
    }

    std::map<std::string, std::string> values;
    std::size_t start = 0;
    while(start < contents.size()) {
        const auto end = contents.find('\n', start);
        const auto line = contents.substr(start, end - start);
        const auto separator = line.find('=');
        if(separator == std::string_view::npos || separator == 0 ||
           separator == line.size() - 1) {
            result.detail = "SESSION_PROFILE_LINE_INVALID";
            return result;
        }
        const auto key = line.substr(0, separator);
        const auto value = line.substr(separator + 1);
        if(std::find(kKeys.begin(), kKeys.end(), key) == kKeys.end() ||
           !values.emplace(std::string(key), std::string(value)).second) {
            result.detail = "SESSION_PROFILE_SCHEMA_INVALID";
            return result;
        }
        start = end + 1;
    }
    if(values.size() != kKeys.size()) {
        result.detail = "SESSION_PROFILE_FIELDS_MISSING";
        return result;
    }

    auto& profile = result.profile;
    profile.profile_id = values["PROFILE_ID"];
    profile.machine = values["MACHINE"];
    profile.drm_device = values["DRM_DEVICE"];
    profile.pixel_format = values["PIXEL_FORMAT"];
    if(!valid_token(profile.profile_id, 128) || !valid_token(profile.machine, 32) ||
       profile.drm_device != "/dev/dri/card0" ||
       (profile.pixel_format != "XRGB8888" && profile.pixel_format != "ARGB8888") ||
       !parse_signed(values["LOGICAL_WIDTH"], 240, 4096, profile.logical_width) ||
       !parse_signed(values["LOGICAL_HEIGHT"], 80, 2048, profile.logical_height) ||
       !parse_unsigned(values["DRM_CONNECTOR_ID"], 1, std::numeric_limits<std::uint32_t>::max(),
                       profile.connector_id) ||
       !parse_unsigned(values["DRM_CRTC_ID"], 1, std::numeric_limits<std::uint32_t>::max(),
                       profile.crtc_id) ||
       !parse_unsigned(values["DRM_OVERLAY_PLANE_ID"], 1,
                       std::numeric_limits<std::uint32_t>::max(), profile.overlay_plane_id) ||
       !parse_signed(values["DRM_OVERLAY_ZPOS"], 0, 128, profile.overlay_zpos) ||
       !parse_signed(values["DISPLAY_X"], 0, 4096, profile.display_x) ||
       !parse_signed(values["DISPLAY_Y"], 0, 4096, profile.display_y) ||
       !parse_signed(values["DISPLAY_WIDTH"], 80, 4096, profile.display_width) ||
       !parse_signed(values["DISPLAY_HEIGHT"], 80, 4096, profile.display_height) ||
       !parse_integer(values["DISPLAY_ROTATION"], std::uint16_t {0}, std::uint16_t {270},
                      profile.display_rotation) ||
       (profile.display_rotation != 0 && profile.display_rotation != 90 &&
        profile.display_rotation != 180 && profile.display_rotation != 270) ||
       (values["HOLE_SESSION_CERTIFIED"] != "1" && values["HOLE_SESSION_CERTIFIED"] != "0") ||
       (values["PERSONAL_UNBOUND"] != "1" && values["PERSONAL_UNBOUND"] != "0") ||
       (values["HOLE_SESSION_CERTIFIED"] != "1" && values["PERSONAL_UNBOUND"] != "1")) {
        result.detail = "SESSION_PROFILE_VALUE_INVALID";
        return result;
    }
    if(profile.display_x > 4096 - profile.display_width ||
       profile.display_y > 4096 - profile.display_height) {
        result.detail = "SESSION_PROFILE_RECTANGLE_INVALID";
        return result;
    }
    profile.personal_unbound = values["PERSONAL_UNBOUND"] == "1";
    profile.hole_session_certified = values["HOLE_SESSION_CERTIFIED"] == "1";
    return result;
}

bool package_supports_session_profile(
    const VerifiedPackageManifest& manifest, const CertifiedSessionProfile& profile) noexcept
{
    if(manifest.app_id != "top.lvgl.platform" || manifest.entry != "bin/lvgl-sessiond") {
        return false;
    }
    if(profile.personal_unbound) return true;
    return std::find(manifest.supported_profiles.begin(), manifest.supported_profiles.end(),
                     profile.profile_id) != manifest.supported_profiles.end() &&
           std::find(manifest.supported_machines.begin(), manifest.supported_machines.end(),
                     profile.machine) != manifest.supported_machines.end();
}

}  // namespace lvgl_platform
