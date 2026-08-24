#include "lvgl_platform/crypto_provider.h"
#include "lvgl_platform/device_profile.h"
#include "lvgl_platform/package_verifier.h"
#include "lvgl_platform/touch_protocol.h"

#include <algorithm>
#include <array>
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

std::uint8_t hex_nibble(char value)
{
    if(value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if(value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    return 0xff;
}

template <std::size_t Size>
std::array<std::uint8_t, Size> decode_hex(const char* value)
{
    std::array<std::uint8_t, Size> output {};
    for(std::size_t index = 0; index < output.size(); ++index) {
        output[index] = static_cast<std::uint8_t>(
            (hex_nibble(value[index * 2]) << 4U) | hex_nibble(value[index * 2 + 1]));
    }
    return output;
}

void test_crypto_provider()
{
    const auto crypto = lvgl_platform::CryptoProvider::load_default();
    expect(crypto != nullptr, "a supported libcrypto provider is available");
    if(crypto == nullptr) return;

    constexpr char message[] = "abc";
    lvgl_platform::Sha256Digest sha256 {};
    lvgl_platform::Sha512Digest sha512 {};
    expect(crypto->sha256(message, 3, sha256), "SHA-256 operation succeeds");
    expect(sha256 == decode_hex<32>(
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
           "SHA-256 matches its known vector");
    expect(crypto->sha512(message, 3, sha512), "SHA-512 operation succeeds");
    expect(sha512 == decode_hex<64>(
               "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
               "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"),
           "SHA-512 matches its known vector");

    const auto public_key = decode_hex<32>(
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    auto signature = decode_hex<64>(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    expect(crypto->verify_ed25519(public_key, nullptr, 0, signature),
           "Ed25519 accepts RFC 8032 test vector 1");
    signature[0] ^= 0x01;
    expect(!crypto->verify_ed25519(public_key, nullptr, 0, signature),
           "Ed25519 rejects a modified signature");
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

void test_package_verifier()
{
    const auto crypto = lvgl_platform::CryptoProvider::load_default();
    expect(crypto != nullptr, "package verifier crypto provider is available");
    if(crypto == nullptr) return;
    const lvgl_platform::TrustedPublicKey test_key {
        decode_hex<32>("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"),
        true};
    const auto signed_text = read_file(LVGL_PLATFORM_TEST_SIGNED_PACKAGE);
    std::vector<std::uint8_t> signed_bytes(signed_text.begin(), signed_text.end());
    const auto verified = lvgl_platform::verify_package(
        signed_bytes.data(), signed_bytes.size(), *crypto, {test_key});
    expect(verified.ok(), verified.detail.c_str());
    expect(verified.signature_verified, "native verifier authenticates the Ed25519 envelope");
    expect(verified.manifest.app_id == "test.lvgl.vector" &&
               verified.manifest.entry == "bin/test-app" && verified.manifest.files.size() == 2,
           "native verifier exposes the authenticated application manifest");

    auto untrusted_key = test_key;
    untrusted_key.production = false;
    expect(lvgl_platform::verify_package(
               signed_bytes.data(), signed_bytes.size(), *crypto, {untrusted_key}).status ==
               lvgl_platform::PackageStatus::signer_untrusted,
           "production verification rejects a known test-only key");

    auto tampered = signed_bytes;
    tampered[tampered.size() - 97] ^= 0x01;
    expect(lvgl_platform::verify_package(
               tampered.data(), tampered.size(), *crypto, {test_key}).status ==
               lvgl_platform::PackageStatus::content_hash_mismatch,
           "native verifier rejects modified payload content");
    tampered = signed_bytes;
    tampered[verified.unsigned_size + 28] ^= 0x01;
    expect(lvgl_platform::verify_package(
               tampered.data(), tampered.size(), *crypto, {test_key}).status ==
               lvgl_platform::PackageStatus::signature_invalid,
           "native verifier rejects a modified signature");

    const auto development_text = read_file(LVGL_PLATFORM_TEST_DEV_PACKAGE);
    std::vector<std::uint8_t> development(development_text.begin(), development_text.end());
    expect(lvgl_platform::verify_package(
               development.data(), development.size(), *crypto, {}).status ==
               lvgl_platform::PackageStatus::development_rejected,
           "native production verifier rejects developer packages");
    expect(lvgl_platform::verify_package(
               development.data(), development.size(), *crypto, {}, true).ok(),
           "native development inspection can validate unsigned structure explicitly");

    auto invalid_utf8 = development;
    const std::string fixture_name = "LVAPP test vector";
    const auto name_at = std::search(
        invalid_utf8.begin(), invalid_utf8.end(), fixture_name.begin(), fixture_name.end());
    expect(name_at != invalid_utf8.end(), "development fixture contains its application name");
    if(name_at != invalid_utf8.end()) *name_at = 0xff;
    expect(lvgl_platform::verify_package(
               invalid_utf8.data(), invalid_utf8.size(), *crypto, {}, true).status ==
               lvgl_platform::PackageStatus::invalid_manifest,
           "native verifier rejects malformed UTF-8 in the canonical manifest");
}

}  // namespace

int main()
{
    test_crypto_provider();
    test_profile();
    test_touch_protocol();
    test_package_verifier();
    if(failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "platform contract tests passed\n";
    return EXIT_SUCCESS;
}
