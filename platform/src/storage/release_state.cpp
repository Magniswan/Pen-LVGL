#include "lvgl_platform/release_state.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string_view>

namespace lvgl_platform {
namespace {

constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kDigestBlockSize = 192;
constexpr std::size_t kChecksumSize = 32;
constexpr std::size_t kMaximumStateSize = 512;
constexpr std::array<std::uint8_t, 8> kMagic {'L', 'V', 'S', 'T', 'A', 'T', 'E', '1'};

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for(std::size_t index = 0; index < 4; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    for(std::size_t index = 0; index < 8; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8U);
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept
{
    std::uint32_t value = 0;
    for(std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* bytes) noexcept
{
    std::uint64_t value = 0;
    for(std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

bool app_id(std::string_view value) noexcept
{
    if(value.empty() || value.size() > 96 || value.front() == '.' || value.back() == '.') {
        return false;
    }
    bool separator = false;
    bool has_separator = false;
    for(const auto character : value) {
        const bool ordinary = (character >= 'a' && character <= 'z') ||
                              (character >= '0' && character <= '9');
        if(character == '.' || character == '-') {
            if(separator) return false;
            separator = true;
            has_separator = true;
        } else if(ordinary) {
            separator = false;
        } else {
            return false;
        }
    }
    return has_separator && !separator;
}

bool key_id(std::string_view value) noexcept
{
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool zero_digest(const Sha512Digest& digest) noexcept
{
    return std::all_of(digest.begin(), digest.end(), [](std::uint8_t byte) { return byte == 0; });
}

bool valid_state(const ApplicationReleaseState& state) noexcept
{
    if(!app_id(state.app_id) || !key_id(state.signing_key_id) || state.generation == 0 ||
       state.current_release == 0 || state.high_release < state.current_release ||
       state.previous_release > state.high_release ||
       state.quarantined_release > state.high_release ||
       state.consecutive_launch_failures > 3 || zero_digest(state.current_digest) ||
       zero_digest(state.high_digest)) {
        return false;
    }
    if((state.previous_release == 0) != zero_digest(state.previous_digest)) return false;
    if(state.high_release == state.current_release && state.high_digest != state.current_digest) {
        return false;
    }
    if(state.previous_release == state.current_release ||
       state.quarantined_release == state.current_release) {
        return false;
    }
    return true;
}

EncodedReleaseState encode_failure(ReleaseStateStatus status, const char* detail)
{
    return {status, detail, {}};
}

DecodedReleaseState decode_failure(ReleaseStateStatus status, const char* detail)
{
    return {status, detail, {}};
}

}  // namespace

EncodedReleaseState encode_release_state(
    const ApplicationReleaseState& state, const CryptoProvider& crypto)
{
    if(!valid_state(state)) {
        return encode_failure(ReleaseStateStatus::invalid_value, "STATE_VALUE_INVALID");
    }
    const auto total_size = kHeaderSize + kDigestBlockSize + state.app_id.size() +
                            state.signing_key_id.size() + kChecksumSize;
    if(total_size > kMaximumStateSize) {
        return encode_failure(ReleaseStateStatus::invalid_layout, "STATE_SIZE_INVALID");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(total_size);
    bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
    append_u16(bytes, 1);
    append_u16(bytes, static_cast<std::uint16_t>(kHeaderSize));
    append_u32(bytes, static_cast<std::uint32_t>(total_size));
    append_u64(bytes, state.generation);
    append_u64(bytes, state.current_release);
    append_u64(bytes, state.previous_release);
    append_u64(bytes, state.high_release);
    append_u64(bytes, state.quarantined_release);
    append_u32(bytes, state.high_security_epoch);
    append_u16(bytes, state.consecutive_launch_failures);
    bytes.push_back(static_cast<std::uint8_t>(state.app_id.size()));
    bytes.push_back(static_cast<std::uint8_t>(state.signing_key_id.size()));
    bytes.insert(bytes.end(), state.current_digest.begin(), state.current_digest.end());
    bytes.insert(bytes.end(), state.previous_digest.begin(), state.previous_digest.end());
    bytes.insert(bytes.end(), state.high_digest.begin(), state.high_digest.end());
    bytes.insert(bytes.end(), state.app_id.begin(), state.app_id.end());
    bytes.insert(bytes.end(), state.signing_key_id.begin(), state.signing_key_id.end());
    Sha256Digest checksum {};
    if(!crypto.sha256(bytes.data(), bytes.size(), checksum)) {
        return encode_failure(ReleaseStateStatus::crypto_unavailable, "STATE_CRYPTO_UNAVAILABLE");
    }
    bytes.insert(bytes.end(), checksum.begin(), checksum.end());
    return {ReleaseStateStatus::valid, "STATE_ENCODED", std::move(bytes)};
}

DecodedReleaseState decode_release_state(
    const std::uint8_t* bytes, std::size_t size, const CryptoProvider& crypto)
{
    if(bytes == nullptr || size < kHeaderSize + kDigestBlockSize + kChecksumSize ||
       size > kMaximumStateSize || !std::equal(kMagic.begin(), kMagic.end(), bytes) ||
       read_u16(bytes + 8) != 1 || read_u16(bytes + 10) != kHeaderSize ||
       read_u32(bytes + 12) != size) {
        return decode_failure(ReleaseStateStatus::invalid_layout, "STATE_LAYOUT_INVALID");
    }
    const auto app_size = bytes[62];
    const auto key_size = bytes[63];
    if(app_size == 0 || app_size > 96 || key_size != 32 ||
       kHeaderSize + kDigestBlockSize + app_size + key_size + kChecksumSize != size) {
        return decode_failure(ReleaseStateStatus::invalid_layout, "STATE_LENGTH_INVALID");
    }
    Sha256Digest checksum {};
    if(!crypto.sha256(bytes, size - kChecksumSize, checksum)) {
        return decode_failure(ReleaseStateStatus::crypto_unavailable, "STATE_CRYPTO_UNAVAILABLE");
    }
    if(!std::equal(checksum.begin(), checksum.end(), bytes + size - kChecksumSize)) {
        return decode_failure(ReleaseStateStatus::checksum_mismatch, "STATE_CHECKSUM_MISMATCH");
    }
    ApplicationReleaseState state;
    state.generation = read_u64(bytes + 16);
    state.current_release = read_u64(bytes + 24);
    state.previous_release = read_u64(bytes + 32);
    state.high_release = read_u64(bytes + 40);
    state.quarantined_release = read_u64(bytes + 48);
    state.high_security_epoch = read_u32(bytes + 56);
    state.consecutive_launch_failures = read_u16(bytes + 60);
    std::copy_n(bytes + kHeaderSize, state.current_digest.size(), state.current_digest.begin());
    std::copy_n(
        bytes + kHeaderSize + state.current_digest.size(), state.previous_digest.size(),
        state.previous_digest.begin());
    std::copy_n(
        bytes + kHeaderSize + state.current_digest.size() + state.previous_digest.size(),
        state.high_digest.size(), state.high_digest.begin());
    const auto* strings = bytes + kHeaderSize + kDigestBlockSize;
    state.app_id.assign(reinterpret_cast<const char*>(strings), app_size);
    state.signing_key_id.assign(reinterpret_cast<const char*>(strings + app_size), key_size);
    if(!valid_state(state)) {
        return decode_failure(ReleaseStateStatus::invalid_value, "STATE_VALUE_INVALID");
    }
    return {ReleaseStateStatus::valid, "STATE_DECODED", std::move(state)};
}

SelectedReleaseState select_release_state(
    const std::vector<std::uint8_t>& slot_a, const std::vector<std::uint8_t>& slot_b,
    const CryptoProvider& crypto)
{
    const auto a = decode_release_state(slot_a.data(), slot_a.size(), crypto);
    const auto b = decode_release_state(slot_b.data(), slot_b.size(), crypto);
    if(!a.ok() && !b.ok()) {
        return {{ReleaseStateStatus::no_valid_slot, "STATE_NO_VALID_SLOT", {}}, 0, true};
    }
    if(a.ok() && b.ok() && a.state.generation == b.state.generation && slot_a != slot_b) {
        return {{ReleaseStateStatus::split_brain, "STATE_SPLIT_BRAIN", {}}, 0, false};
    }
    if(a.ok() && (!b.ok() || a.state.generation >= b.state.generation)) {
        return {{a.status, a.detail, a.state}, 1, !b.ok()};
    }
    return {{b.status, b.detail, b.state}, 2, !a.ok()};
}

std::optional<ApplicationReleaseState> activation_state_for(
    const PackageVerification& candidate, const Sha512Digest& package_digest,
    const std::optional<ApplicationReleaseState>& current)
{
    if(!candidate.ok() || candidate.development || !candidate.signature_verified ||
       candidate.manifest.release_counter == 0 || zero_digest(package_digest)) {
        return std::nullopt;
    }
    if(current.has_value()) {
        if(!valid_state(*current) || current->app_id != candidate.manifest.app_id ||
           current->signing_key_id != candidate.manifest.signing_key_id ||
           candidate.manifest.release_counter < current->high_release ||
           candidate.manifest.security_epoch < current->high_security_epoch ||
           candidate.manifest.release_counter == current->quarantined_release ||
           (candidate.manifest.release_counter == current->high_release &&
            package_digest != current->high_digest)) {
            return std::nullopt;
        }
        if(candidate.manifest.release_counter == current->current_release &&
           package_digest == current->current_digest) {
            return *current;
        }
    }

    ApplicationReleaseState next;
    next.app_id = candidate.manifest.app_id;
    next.signing_key_id = candidate.manifest.signing_key_id;
    next.generation = current.has_value() ? current->generation + 1 : 1;
    if(next.generation == 0) return std::nullopt;
    next.current_release = candidate.manifest.release_counter;
    next.current_digest = package_digest;
    next.high_release = candidate.manifest.release_counter;
    next.high_security_epoch = candidate.manifest.security_epoch;
    next.high_digest = package_digest;
    if(current.has_value()) {
        next.previous_release = current->current_release;
        next.previous_digest = current->current_digest;
        next.high_release = std::max(next.high_release, current->high_release);
        next.high_security_epoch = std::max(next.high_security_epoch, current->high_security_epoch);
        if(next.high_release == current->high_release) next.high_digest = current->high_digest;
        next.quarantined_release = current->quarantined_release;
    }
    if(next.previous_release == next.current_release) {
        next.previous_release = 0;
        next.previous_digest.fill(0);
    }
    if(next.quarantined_release == next.current_release) next.quarantined_release = 0;
    return valid_state(next) ? std::optional<ApplicationReleaseState>(std::move(next)) : std::nullopt;
}

std::optional<ApplicationReleaseState> rollback_failed_release(
    const ApplicationReleaseState& current)
{
    if(!valid_state(current) || current.previous_release == 0 ||
       current.generation == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    auto next = current;
    ++next.generation;
    next.quarantined_release = current.current_release;
    next.current_release = current.previous_release;
    next.current_digest = current.previous_digest;
    next.previous_release = 0;
    next.previous_digest.fill(0);
    next.consecutive_launch_failures = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(3, current.consecutive_launch_failures + 1U));
    return valid_state(next) ? std::optional<ApplicationReleaseState>(std::move(next)) : std::nullopt;
}

}  // namespace lvgl_platform
