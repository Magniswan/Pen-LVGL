#pragma once

#include <cstdint>

namespace lvgl_platform {

enum class ErrorDomain : std::uint16_t {
    none = 0,
    profile = 1,
    trust = 2,
    package = 3,
    policy = 4,
    storage = 5,
    runtime = 6,
    display = 7,
    input = 8,
    application = 9,
};

struct ErrorCode {
    ErrorDomain domain {ErrorDomain::none};
    std::uint16_t value {0};

    constexpr bool ok() const noexcept
    {
        return domain == ErrorDomain::none && value == 0;
    }

    friend constexpr bool operator==(ErrorCode lhs, ErrorCode rhs) noexcept
    {
        return lhs.domain == rhs.domain && lhs.value == rhs.value;
    }
};

namespace errors {
inline constexpr ErrorCode ok {};
inline constexpr ErrorCode profile_invalid {ErrorDomain::profile, 1};
inline constexpr ErrorCode profile_unsupported_schema {ErrorDomain::profile, 2};
inline constexpr ErrorCode profile_no_match {ErrorDomain::profile, 3};
inline constexpr ErrorCode profile_ambiguous {ErrorDomain::profile, 4};
inline constexpr ErrorCode input_frame_invalid {ErrorDomain::input, 1};
inline constexpr ErrorCode input_session_mismatch {ErrorDomain::input, 2};
inline constexpr ErrorCode input_sequence_replayed {ErrorDomain::input, 3};
inline constexpr ErrorCode input_timestamp_stale {ErrorDomain::input, 4};
inline constexpr ErrorCode input_contact_invalid {ErrorDomain::input, 5};
}  // namespace errors

}  // namespace lvgl_platform
