#include "lvgl_platform/device_profile.h"
#include "lvgl_platform/touch_protocol.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if(condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

std::string read_file(const char* path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void test_profile()
{
    const auto parsed = lvgl_platform::parse_device_profile(
        read_file(LVGL_PLATFORM_TEST_PROFILE));
    expect(parsed.ok(), parsed.detail.c_str());
    if(!parsed.ok()) return;

    const auto& profile = parsed.profile;
    expect(lvgl_platform::logical_to_physical({0, 0}, profile) ==
               lvgl_platform::Point {107, 959},
           "top-left logical coordinate maps to the profiled physical corner");
    expect(lvgl_platform::logical_to_physical({959, 265}, profile) ==
               lvgl_platform::Point {372, 0},
           "bottom-right logical coordinate maps to the profiled physical corner");
    expect(lvgl_platform::physical_touch_to_logical({113, 959}, profile) ==
               lvgl_platform::Point {0, 0},
           "touch transform maps its profiled top-left corner");
    expect(lvgl_platform::physical_touch_to_logical({378, 0}, profile) ==
               lvgl_platform::Point {959, 265},
           "touch transform maps its profiled bottom-right corner");

    lvgl_platform::DeviceIdentity identity {
        "OVERHEAD_Y01_SKU_CHN_PRO", "4.8.6", "RK3562_Orange_V0", "aarch64",
        "glibc-2.36", 64};
    std::vector<lvgl_platform::DeviceProfile> profiles {profile};
    expect(lvgl_platform::select_device_profile(profiles, identity).ok(),
           "exact identity selects one profile");
    identity.firmware = "4.8.7";
    expect(lvgl_platform::select_device_profile(profiles, identity).error ==
               lvgl_platform::errors::profile_no_match,
           "different firmware fails closed");
    identity.firmware = "4.8.6";
    profiles.push_back(profile);
    expect(lvgl_platform::select_device_profile(profiles, identity).error ==
               lvgl_platform::errors::profile_ambiguous,
           "ambiguous profiles fail closed");

    auto invalid = read_file(LVGL_PLATFORM_TEST_PROFILE);
    const auto marker = invalid.find("\"schemaVersion\": 1");
    expect(marker != std::string::npos, "profile fixture contains schema marker");
    if(marker != std::string::npos) invalid.replace(marker, 18, "\"schemaVersion\": 2");
    expect(lvgl_platform::parse_device_profile(invalid).error ==
               lvgl_platform::errors::profile_unsupported_schema,
           "unknown profile schema fails closed");

    invalid = read_file(LVGL_PLATFORM_TEST_PROFILE);
    const auto end = invalid.rfind('}');
    expect(end != std::string::npos, "profile fixture has a root terminator");
    if(end != std::string::npos) invalid.insert(end, ", \"unrecognized\": true");
    expect(lvgl_platform::parse_device_profile(invalid).error ==
               lvgl_platform::errors::profile_invalid,
           "unknown profile members fail closed");
}

void test_touch_protocol()
{
    const lvgl_platform::TouchFrame frame {
        0x123456789ABCDEF0ULL, 7, 1234567, lvgl_platform::TouchPhase::move, 2,
        811, 42, 0};
    const auto encoded = lvgl_platform::encode_touch_frame(frame);
    const auto decoded = lvgl_platform::decode_touch_frame(encoded.data(), encoded.size(), 960, 266);
    expect(decoded.ok(), "valid touch frame decodes");
    expect(decoded.frame.session_nonce == frame.session_nonce &&
               decoded.frame.sequence == frame.sequence && decoded.frame.phase == frame.phase &&
               decoded.frame.x == frame.x && decoded.frame.y == frame.y,
           "touch frame round trip preserves fields");

    auto corrupt = encoded;
    corrupt[0] ^= 0x01;
    expect(!lvgl_platform::decode_touch_frame(corrupt.data(), corrupt.size(), 960, 266).ok(),
           "bad touch magic is rejected");
    expect(!lvgl_platform::decode_touch_frame(encoded.data(), encoded.size() - 1, 960, 266).ok(),
           "truncated touch frame is rejected");

    auto out_of_bounds = frame;
    out_of_bounds.x = 960;
    expect(!lvgl_platform::valid_touch_frame(out_of_bounds, 960, 266),
           "out-of-bounds touch is rejected");

    lvgl_platform::TouchSequenceGuard guard;
    guard.reset(frame.session_nonce);
    expect(guard.accept(frame).ok(), "first frame in a session is accepted");
    expect(guard.accept(frame) == lvgl_platform::errors::input_sequence_replayed,
           "replayed sequence is rejected");
    auto wrong_session = frame;
    ++wrong_session.session_nonce;
    ++wrong_session.sequence;
    expect(guard.accept(wrong_session) == lvgl_platform::errors::input_session_mismatch,
           "different session nonce is rejected");
}

}  // namespace

int main()
{
    test_profile();
    test_touch_protocol();
    if(failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "platform contract tests passed\n";
    return EXIT_SUCCESS;
}
