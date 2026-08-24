#include "lvgl_platform/installer_protocol.h"

#include "lvgl_platform/session_control.h"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>

namespace lvgl_platform {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic {'L', 'V', 'I', 'N', 'S', 'T', '1', 0};
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kRequestTokenOffset = 32;
constexpr std::size_t kResponseStringsOffset = 80;
constexpr std::size_t kTokenLimit = 128;
constexpr std::size_t kAppIdLimit = 96;
constexpr std::size_t kNameLimit = 96;
constexpr std::size_t kVersionLimit = 32;
constexpr std::size_t kDetailLimit = 256;

template <typename Integer>
void put_le(Integer value, std::uint8_t* output) noexcept
{
    static_assert(std::is_unsigned_v<Integer>);
    for(std::size_t index = 0; index < sizeof(Integer); ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

template <typename Integer>
Integer get_le(const std::uint8_t* input) noexcept
{
    static_assert(std::is_unsigned_v<Integer>);
    Integer value = 0;
    for(std::size_t index = 0; index < sizeof(Integer); ++index) {
        value |= static_cast<Integer>(input[index]) << (index * 8U);
    }
    return value;
}

bool valid_command(InstallerCommand command) noexcept
{
    return command == InstallerCommand::scan || command == InstallerCommand::candidate ||
           command == InstallerCommand::install;
}

bool valid_status(InstallerProtocolStatus status) noexcept
{
    return status >= InstallerProtocolStatus::ready &&
           status <= InstallerProtocolStatus::io_error;
}

bool valid_token(std::string_view token) noexcept
{
    return token.size() == kTokenLimit &&
           std::all_of(token.begin(), token.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool valid_text(std::string_view text, std::size_t maximum, bool allow_empty) noexcept
{
    if((text.empty() && !allow_empty) || text.size() > maximum) return false;
    return std::all_of(text.begin(), text.end(), [](unsigned char character) {
        return character >= 0x20 && character != 0x7f;
    });
}

bool all_zero(const std::uint8_t* begin, const std::uint8_t* end) noexcept
{
    return std::all_of(begin, end, [](std::uint8_t value) { return value == 0; });
}

bool valid_candidate(const InboxCandidate& candidate) noexcept
{
    return valid_token(candidate.token) && valid_session_app_id(candidate.app_id) &&
           valid_text(candidate.name, kNameLimit, false) &&
           valid_text(candidate.version, kVersionLimit, false) &&
           valid_text(candidate.detail, kDetailLimit, false) && candidate.release_counter != 0 &&
           candidate.package_size != 0;
}

void copy_text(std::string_view value, std::uint8_t* output) noexcept
{
    std::copy(value.begin(), value.end(), output);
}

}  // namespace

std::array<std::uint8_t, kInstallerRequestSize> encode_installer_request(
    const InstallerRequest& request) noexcept
{
    std::array<std::uint8_t, kInstallerRequestSize> output {};
    const bool token_command = request.command == InstallerCommand::install;
    if(!valid_command(request.command) || request.request_id == 0 ||
       (request.command != InstallerCommand::candidate && request.index != 0) ||
       (token_command ? !valid_token(request.token) : !request.token.empty())) {
        return output;
    }
    std::copy(kMagic.begin(), kMagic.end(), output.begin());
    put_le(kVersion, output.data() + 8);
    put_le(static_cast<std::uint16_t>(request.command), output.data() + 10);
    put_le(request.request_id, output.data() + 16);
    put_le(request.index, output.data() + 24);
    put_le(static_cast<std::uint16_t>(request.token.size()), output.data() + 28);
    copy_text(request.token, output.data() + kRequestTokenOffset);
    return output;
}

bool decode_installer_request(
    const std::uint8_t* bytes, std::size_t size, InstallerRequest& request) noexcept
{
    if(bytes == nullptr || size != kInstallerRequestSize ||
       !std::equal(kMagic.begin(), kMagic.end(), bytes) ||
       get_le<std::uint16_t>(bytes + 8) != kVersion ||
       get_le<std::uint32_t>(bytes + 12) != 0 || get_le<std::uint16_t>(bytes + 30) != 0) {
        return false;
    }
    const auto command = static_cast<InstallerCommand>(get_le<std::uint16_t>(bytes + 10));
    const auto request_id = get_le<std::uint64_t>(bytes + 16);
    const auto index = get_le<std::uint32_t>(bytes + 24);
    const auto token_size = get_le<std::uint16_t>(bytes + 28);
    if(!valid_command(command) || request_id == 0 || token_size > kTokenLimit ||
       !all_zero(bytes + kRequestTokenOffset + token_size, bytes + size)) {
        return false;
    }
    std::string token(
        reinterpret_cast<const char*>(bytes + kRequestTokenOffset), token_size);
    const bool token_command = command == InstallerCommand::install;
    if((command != InstallerCommand::candidate && index != 0) ||
       (token_command ? !valid_token(token) : !token.empty())) {
        return false;
    }
    request = {command, request_id, index, std::move(token)};
    return true;
}

std::array<std::uint8_t, kInstallerResponseSize> encode_installer_response(
    const InstallerResponse& response) noexcept
{
    std::array<std::uint8_t, kInstallerResponseSize> output {};
    const bool candidate = response.command == InstallerCommand::candidate &&
                           response.status == InstallerProtocolStatus::ready;
    if(!valid_command(response.command) || !valid_status(response.status) ||
       response.request_id == 0 || !valid_text(response.detail, kDetailLimit, false) ||
       (candidate && !valid_candidate(response.candidate)) ||
       (!candidate && (!response.candidate.token.empty() || !response.candidate.app_id.empty())) ||
       (response.command != InstallerCommand::scan && response.count != 0)) {
        return output;
    }
    std::copy(kMagic.begin(), kMagic.end(), output.begin());
    put_le(kVersion, output.data() + 8);
    put_le(static_cast<std::uint16_t>(response.command), output.data() + 10);
    put_le(static_cast<std::uint16_t>(response.status), output.data() + 12);
    put_le(static_cast<std::uint16_t>(candidate && response.candidate.installable ? 1 : 0),
           output.data() + 14);
    put_le(response.request_id, output.data() + 16);
    put_le(response.count, output.data() + 24);
    put_le(candidate ? response.candidate.release_counter : 0, output.data() + 32);
    put_le(candidate ? response.candidate.package_size : 0, output.data() + 40);
    put_le(candidate ? response.candidate.security_epoch : 0, output.data() + 48);
    const std::string_view token = candidate ? response.candidate.token : std::string_view {};
    const std::string_view app_id = candidate ? response.candidate.app_id : std::string_view {};
    const std::string_view name = candidate ? response.candidate.name : std::string_view {};
    const std::string_view version = candidate ? response.candidate.version : std::string_view {};
    put_le(static_cast<std::uint16_t>(token.size()), output.data() + 52);
    put_le(static_cast<std::uint16_t>(app_id.size()), output.data() + 54);
    put_le(static_cast<std::uint16_t>(name.size()), output.data() + 56);
    put_le(static_cast<std::uint16_t>(version.size()), output.data() + 58);
    put_le(static_cast<std::uint16_t>(response.detail.size()), output.data() + 60);
    auto* cursor = output.data() + kResponseStringsOffset;
    copy_text(token, cursor);
    cursor += kTokenLimit;
    copy_text(app_id, cursor);
    cursor += kAppIdLimit;
    copy_text(name, cursor);
    cursor += kNameLimit;
    copy_text(version, cursor);
    cursor += kVersionLimit;
    copy_text(response.detail, cursor);
    return output;
}

bool decode_installer_response(
    const std::uint8_t* bytes, std::size_t size, InstallerResponse& response) noexcept
{
    if(bytes == nullptr || size != kInstallerResponseSize ||
       !std::equal(kMagic.begin(), kMagic.end(), bytes) ||
       get_le<std::uint16_t>(bytes + 8) != kVersion ||
       !all_zero(bytes + 28, bytes + 32) || !all_zero(bytes + 62, bytes + 80)) {
        return false;
    }
    const auto command = static_cast<InstallerCommand>(get_le<std::uint16_t>(bytes + 10));
    const auto status = static_cast<InstallerProtocolStatus>(get_le<std::uint16_t>(bytes + 12));
    const auto flags = get_le<std::uint16_t>(bytes + 14);
    const auto request_id = get_le<std::uint64_t>(bytes + 16);
    const auto count = get_le<std::uint32_t>(bytes + 24);
    const auto release = get_le<std::uint64_t>(bytes + 32);
    const auto package_size = get_le<std::uint64_t>(bytes + 40);
    const auto epoch = get_le<std::uint32_t>(bytes + 48);
    const std::size_t token_size = get_le<std::uint16_t>(bytes + 52);
    const std::size_t app_size = get_le<std::uint16_t>(bytes + 54);
    const std::size_t name_size = get_le<std::uint16_t>(bytes + 56);
    const std::size_t version_size = get_le<std::uint16_t>(bytes + 58);
    const std::size_t detail_size = get_le<std::uint16_t>(bytes + 60);
    if(!valid_command(command) || !valid_status(status) || request_id == 0 || flags > 1 ||
       token_size > kTokenLimit || app_size > kAppIdLimit || name_size > kNameLimit ||
       version_size > kVersionLimit || detail_size > kDetailLimit ||
       !all_zero(bytes + 80 + token_size, bytes + 80 + kTokenLimit) ||
       !all_zero(bytes + 208 + app_size, bytes + 208 + kAppIdLimit) ||
       !all_zero(bytes + 304 + name_size, bytes + 304 + kNameLimit) ||
       !all_zero(bytes + 400 + version_size, bytes + 400 + kVersionLimit) ||
       !all_zero(bytes + 432 + detail_size, bytes + size)) {
        return false;
    }
    InstallerResponse decoded;
    decoded.command = command;
    decoded.status = status;
    decoded.request_id = request_id;
    decoded.count = count;
    decoded.detail.assign(reinterpret_cast<const char*>(bytes + 432), detail_size);
    const bool candidate = command == InstallerCommand::candidate &&
                           status == InstallerProtocolStatus::ready;
    if(candidate) {
        decoded.candidate.token.assign(reinterpret_cast<const char*>(bytes + 80), token_size);
        decoded.candidate.app_id.assign(reinterpret_cast<const char*>(bytes + 208), app_size);
        decoded.candidate.name.assign(reinterpret_cast<const char*>(bytes + 304), name_size);
        decoded.candidate.version.assign(reinterpret_cast<const char*>(bytes + 400), version_size);
        decoded.candidate.detail = decoded.detail;
        decoded.candidate.release_counter = release;
        decoded.candidate.security_epoch = epoch;
        decoded.candidate.package_size = package_size;
        decoded.candidate.installable = flags == 1;
        if(!valid_candidate(decoded.candidate) || count != 0) return false;
    } else if(flags != 0 || token_size != 0 || app_size != 0 || name_size != 0 ||
              version_size != 0 || release != 0 || package_size != 0 || epoch != 0 ||
              (command != InstallerCommand::scan && count != 0)) {
        return false;
    }
    if(!valid_text(decoded.detail, kDetailLimit, false)) return false;
    response = std::move(decoded);
    return true;
}

}  // namespace lvgl_platform
