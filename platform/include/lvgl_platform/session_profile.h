#pragma once

#include "lvgl_platform/package_verifier.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace lvgl_platform {

struct CertifiedSessionProfile {
    std::string profile_id;
    std::string machine;
    std::string drm_device;
    std::string pixel_format;
    std::int32_t logical_width {0};
    std::int32_t logical_height {0};
    std::uint32_t connector_id {0};
    std::uint32_t crtc_id {0};
    std::uint32_t overlay_plane_id {0};
    std::int32_t overlay_zpos {0};
    std::int32_t display_x {0};
    std::int32_t display_y {0};
    std::int32_t display_width {0};
    std::int32_t display_height {0};
    std::uint16_t display_rotation {0};
    bool hole_session_certified {false};
    bool personal_unbound {false};
};

struct SessionProfileParseResult {
    CertifiedSessionProfile profile;
    std::string detail;

    bool ok() const noexcept { return detail.empty(); }
};

// profile.env is part of the signed platform package. Its intentionally tiny,
// closed schema keeps the privileged session daemon independent from a shell.
SessionProfileParseResult parse_session_profile(std::string_view contents);
bool package_supports_session_profile(
    const VerifiedPackageManifest& manifest, const CertifiedSessionProfile& profile) noexcept;

}  // namespace lvgl_platform
