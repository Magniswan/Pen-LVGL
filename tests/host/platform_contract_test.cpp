#include "lvgl_platform/crypto_provider.h"
#include "lvgl_platform/device_profile.h"
#include "lvgl_platform/package_verifier.h"
#include "lvgl_platform/package_installer.h"
#include "lvgl_platform/rollback_policy.h"
#include "lvgl_platform/release_state.h"
#include "lvgl_platform/safe_path.h"
#include "lvgl_platform/state_store.h"
#include "lvgl_platform/touch_protocol.h"
#include "lvgl_platform/trust_store.h"

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

void test_trust_and_rollback_policy()
{
    constexpr char test_key_hex[] =
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
    const auto official_store = lvgl_platform::OfficialTrustStore::compiled();
    expect(!official_store.configured(),
           "developer builds do not silently acquire an official trust root");

    const auto crypto = lvgl_platform::CryptoProvider::load_default();
    expect(crypto != nullptr, "policy test crypto provider is available");
    if(crypto == nullptr) return;
    const auto signed_text = read_file(LVGL_PLATFORM_TEST_SIGNED_PACKAGE);
    std::vector<std::uint8_t> bytes(signed_text.begin(), signed_text.end());
    expect(official_store.verify(bytes.data(), bytes.size(), *crypto).status ==
               lvgl_platform::PackageStatus::signer_untrusted,
           "official verification API fails closed without its build-time trust root");

    const lvgl_platform::TrustedPublicKey test_key {decode_hex<32>(test_key_hex), true};
    auto candidate = lvgl_platform::verify_package(
        bytes.data(), bytes.size(), *crypto, {test_key});
    expect(candidate.ok(), "authenticated fixture is available to policy tests");
    lvgl_platform::Sha512Digest package_digest {};
    expect(crypto->sha512(bytes.data(), bytes.size(), package_digest),
           "policy test computes a stable whole-package digest");
#if defined(_WIN32)
    expect(lvgl_platform::stage_verified_release(
               "C:/unused", bytes.data(), bytes.size(), candidate, package_digest).status ==
               lvgl_platform::StorageStatus::unsupported_platform,
           "safe release extraction fails explicitly on non-POSIX hosts");
#endif

    const lvgl_platform::InstallPolicyContext context {
        "1.0.0", "1.0", "youdao-y01-4.8.6", "aarch64", {"storage.private"}};
    expect(lvgl_platform::install_official_package(
               "C:/unused", bytes.data(), bytes.size(), context, *crypto).status ==
               lvgl_platform::InstallerStatus::trust_rejected,
           "production installer cannot run without its compiled official key");
    expect(lvgl_platform::install_official_package_with_state_root(
               "C:/payload", "C:/policy", bytes.data(), bytes.size(), context, *crypto).status ==
               lvgl_platform::InstallerStatus::trust_rejected,
           "split payload and policy roots retain the same official-only trust gate");
    expect(lvgl_platform::evaluate_install_policy(candidate, package_digest, context).allowed(),
           "compatible first install is allowed");

    const auto high_water = lvgl_platform::advance_high_water_mark(candidate, package_digest);
    expect(lvgl_platform::evaluate_install_policy(
               candidate, package_digest, context, high_water).allowed(),
           "an identical package reinstall is idempotent");

    auto different_digest = package_digest;
    different_digest[0] ^= 0x01;
    expect(lvgl_platform::evaluate_install_policy(
               candidate, different_digest, context, high_water).status ==
               lvgl_platform::InstallPolicyStatus::release_counter_collision,
           "same release counter with different authenticated bytes is rejected");

    auto newer_candidate = candidate;
    ++newer_candidate.manifest.release_counter;
    expect(lvgl_platform::evaluate_install_policy(
               newer_candidate, different_digest, context, high_water).allowed(),
           "a higher authenticated release counter is accepted");
    const auto advanced = lvgl_platform::advance_high_water_mark(
        newer_candidate, different_digest, high_water);
    expect(advanced.release_counter == newer_candidate.manifest.release_counter &&
               advanced.package_digest == different_digest,
           "accepted update advances the high-water mark and package identity");
    expect(lvgl_platform::advance_high_water_mark(
               candidate, package_digest, advanced).release_counter == advanced.release_counter,
           "high-water advancement is monotonic even if called defensively on stale input");

    auto future = high_water;
    future.release_counter = candidate.manifest.release_counter + 1;
    expect(lvgl_platform::evaluate_install_policy(
               candidate, package_digest, context, future).status ==
               lvgl_platform::InstallPolicyStatus::release_counter_rollback,
           "release counter downgrade is rejected");
    future = high_water;
    future.security_epoch = candidate.manifest.security_epoch + 1;
    expect(lvgl_platform::evaluate_install_policy(
               candidate, package_digest, context, future).status ==
               lvgl_platform::InstallPolicyStatus::security_epoch_rollback,
           "security epoch downgrade is rejected");
    future = high_water;
    future.app_id = "another.application";
    expect(lvgl_platform::evaluate_install_policy(
               candidate, package_digest, context, future).status ==
               lvgl_platform::InstallPolicyStatus::application_id_mismatch,
           "application identity takeover is rejected");
    future = high_water;
    future.signing_key_id = std::string(32, 'f');
    expect(lvgl_platform::evaluate_install_policy(
               candidate, package_digest, context, future).status ==
               lvgl_platform::InstallPolicyStatus::signer_changed,
           "unexpected signer transition is rejected");

    auto incompatible = context;
    incompatible.allowed_capabilities.clear();
    expect(lvgl_platform::evaluate_install_policy(
               candidate, package_digest, incompatible).status ==
               lvgl_platform::InstallPolicyStatus::capability_denied,
           "unavailable capabilities fail closed");
    incompatible = context;
    incompatible.profile_id = "unknown-profile";
    expect(lvgl_platform::evaluate_install_policy(
               candidate, package_digest, incompatible).status ==
               lvgl_platform::InstallPolicyStatus::unsupported_profile,
           "unsupported device profile fails closed");
    incompatible = context;
    incompatible.platform_version = "0.9.9";
    expect(lvgl_platform::evaluate_install_policy(
               candidate, package_digest, incompatible).status ==
               lvgl_platform::InstallPolicyStatus::platform_too_old,
           "minimum platform version is enforced");

    auto unauthenticated = candidate;
    unauthenticated.signature_verified = false;
    expect(lvgl_platform::evaluate_install_policy(
               unauthenticated, package_digest, context).status ==
               lvgl_platform::InstallPolicyStatus::package_not_authenticated,
           "policy cannot be bypassed with a fabricated manifest");
}

void test_release_state()
{
    const auto crypto = lvgl_platform::CryptoProvider::load_default();
    expect(crypto != nullptr, "release state crypto provider is available");
    if(crypto == nullptr) return;
    const auto signed_text = read_file(LVGL_PLATFORM_TEST_SIGNED_PACKAGE);
    std::vector<std::uint8_t> bytes(signed_text.begin(), signed_text.end());
    const lvgl_platform::TrustedPublicKey test_key {
        decode_hex<32>("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"),
        true};
    auto candidate = lvgl_platform::verify_package(
        bytes.data(), bytes.size(), *crypto, {test_key});
    lvgl_platform::Sha512Digest digest_one {};
    expect(candidate.ok() && crypto->sha512(bytes.data(), bytes.size(), digest_one),
           "authenticated fixture initializes release state tests");

    const auto first = lvgl_platform::activation_state_for(candidate, digest_one);
    expect(first.has_value() && first->generation == 1 && first->current_release == 1 &&
               first->previous_release == 0 && first->high_release == 1,
           "first activation creates a monotonic initial state");
    if(!first.has_value()) return;
    const auto encoded_one = lvgl_platform::encode_release_state(*first, *crypto);
    expect(encoded_one.ok(), encoded_one.detail.c_str());
#if defined(_WIN32)
    expect(lvgl_platform::persist_release_state("C:/unused", *first, *crypto).status ==
               lvgl_platform::StateStoreStatus::unsupported_platform &&
               lvgl_platform::load_release_state("C:/unused", first->app_id, *crypto).status ==
                   lvgl_platform::StateStoreStatus::unsupported_platform,
           "atomic state storage fails explicitly on non-POSIX hosts");
#endif
    const auto decoded_one = lvgl_platform::decode_release_state(
        encoded_one.bytes.data(), encoded_one.bytes.size(), *crypto);
    expect(decoded_one.ok() && decoded_one.state.app_id == first->app_id &&
               decoded_one.state.current_digest == digest_one,
           "release state survives a canonical binary round trip");
    const auto idempotent = lvgl_platform::activation_state_for(candidate, digest_one, first);
    expect(idempotent.has_value() && idempotent->generation == first->generation &&
               idempotent->current_digest == first->current_digest,
           "activating the current identical release is idempotent");

    auto tampered = encoded_one.bytes;
    tampered[24] ^= 0x01;
    expect(lvgl_platform::decode_release_state(
               tampered.data(), tampered.size(), *crypto).status ==
               lvgl_platform::ReleaseStateStatus::checksum_mismatch,
           "state checksum detects a modified release counter");
    expect(lvgl_platform::decode_release_state(
               encoded_one.bytes.data(), encoded_one.bytes.size() - 1, *crypto).status ==
               lvgl_platform::ReleaseStateStatus::invalid_layout,
           "truncated release state is rejected");

    auto candidate_two = candidate;
    candidate_two.manifest.release_counter = 2;
    auto digest_two = digest_one;
    digest_two[0] ^= 0x02;
    const auto second = lvgl_platform::activation_state_for(candidate_two, digest_two, first);
    expect(second.has_value() && second->generation == 2 && second->current_release == 2 &&
               second->previous_release == 1 && second->high_release == 2,
           "update activation preserves one previous release");
    if(!second.has_value()) return;
    const auto encoded_two = lvgl_platform::encode_release_state(*second, *crypto);
    const auto selected = lvgl_platform::select_release_state(
        encoded_one.bytes, encoded_two.bytes, *crypto);
    expect(selected.ok() && selected.slot == 2 && selected.state.generation == 2 &&
               !selected.redundancy_degraded,
           "dual-slot recovery selects the newest complete generation");
    const auto degraded = lvgl_platform::select_release_state({}, encoded_two.bytes, *crypto);
    expect(degraded.ok() && degraded.slot == 2 && degraded.redundancy_degraded,
           "one corrupt or missing slot recovers in explicitly degraded mode");

    auto divergent = *second;
    divergent.consecutive_launch_failures = 1;
    const auto encoded_divergent = lvgl_platform::encode_release_state(divergent, *crypto);
    expect(lvgl_platform::select_release_state(
               encoded_two.bytes, encoded_divergent.bytes, *crypto).status ==
               lvgl_platform::ReleaseStateStatus::split_brain,
           "same-generation disagreement fails closed as split brain");

    const auto rolled_back = lvgl_platform::rollback_failed_release(*second);
    expect(rolled_back.has_value() && rolled_back->generation == 3 &&
               rolled_back->current_release == 1 && rolled_back->previous_release == 0 &&
               rolled_back->quarantined_release == 2 && rolled_back->high_release == 2,
           "failed first launch rolls back once while retaining the high-water mark");
    if(rolled_back.has_value()) {
        expect(!lvgl_platform::activation_state_for(candidate_two, digest_two, rolled_back).has_value(),
               "a quarantined release cannot be reactivated by reinstalling identical bytes");
        auto candidate_three = candidate;
        candidate_three.manifest.release_counter = 3;
        auto digest_three = digest_two;
        digest_three[1] ^= 0x03;
        const auto third = lvgl_platform::activation_state_for(
            candidate_three, digest_three, rolled_back);
        expect(third.has_value() && third->current_release == 3 &&
                   third->previous_release == 1 && third->high_release == 3,
               "a newer official release can supersede a quarantined release");
    }
}

}  // namespace

int main()
{
    test_crypto_provider();
    test_profile();
    test_touch_protocol();
    test_package_verifier();
    test_trust_and_rollback_policy();
    test_release_state();
    if(failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "platform contract tests passed\n";
    return EXIT_SUCCESS;
}
