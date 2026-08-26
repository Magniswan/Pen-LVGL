#include "runtime/installer_client.h"

#include "lvgl_platform/installer_protocol.h"

#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dictpen {
namespace {

#if !defined(_WIN32)
int inherited_installer_descriptor() noexcept
{
    const char* value = std::getenv("LVGL_INSTALLER_FD");
    if(value == nullptr || *value == '\0') return -1;
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if(errno != 0 || end == value || *end != '\0' || parsed < 3 || parsed > INT_MAX) return -1;
    const int descriptor = static_cast<int>(parsed);
    struct stat details {};
    int type = 0;
    socklen_t type_size = sizeof(type);
    ucred peer {};
    socklen_t peer_size = sizeof(peer);
    if(::fstat(descriptor, &details) != 0 || !S_ISSOCK(details.st_mode) ||
       ::getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &type_size) != 0 ||
       type != SOCK_SEQPACKET ||
       ::getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0 ||
       peer_size != sizeof(peer) || peer.uid != 0) {
        return -1;
    }
    return ::fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
}
#endif

lvgl_platform::InboxStatus inbox_status(lvgl_platform::InstallerProtocolStatus status) noexcept
{
    using Protocol = lvgl_platform::InstallerProtocolStatus;
    switch(status) {
        case Protocol::ready: return lvgl_platform::InboxStatus::ready;
        case Protocol::empty: return lvgl_platform::InboxStatus::empty;
        case Protocol::invalid_request: return lvgl_platform::InboxStatus::invalid_token;
        case Protocol::rejected: return lvgl_platform::InboxStatus::candidate_rejected;
        case Protocol::not_found: return lvgl_platform::InboxStatus::candidate_not_found;
        case Protocol::unavailable: return lvgl_platform::InboxStatus::root_untrusted;
        case Protocol::io_error: return lvgl_platform::InboxStatus::io_error;
    }
    return lvgl_platform::InboxStatus::io_error;
}

}  // namespace

InstallerClient::InstallerClient() noexcept
{
#if !defined(_WIN32)
    descriptor_ = inherited_installer_descriptor();
#endif
}

InstallerClient::~InstallerClient()
{
#if !defined(_WIN32)
    if(descriptor_ >= 0) ::close(descriptor_);
#endif
}

bool InstallerClient::available() const noexcept { return descriptor_ >= 0; }

bool InstallerClient::exchange(
    const lvgl_platform::InstallerRequest& request,
    lvgl_platform::InstallerResponse& response) noexcept
{
#if defined(_WIN32)
    (void)request;
    (void)response;
    return false;
#else
    if(descriptor_ < 0) return false;
    const auto encoded = lvgl_platform::encode_installer_request(request);
    if(::send(descriptor_, encoded.data(), encoded.size(), MSG_NOSIGNAL) !=
       static_cast<ssize_t>(encoded.size())) {
        return false;
    }
    std::array<std::uint8_t, lvgl_platform::kInstallerResponseSize> bytes {};
    const auto received = ::recv(descriptor_, bytes.data(), bytes.size(), MSG_TRUNC);
    return received == static_cast<ssize_t>(bytes.size()) &&
           lvgl_platform::decode_installer_response(bytes.data(), bytes.size(), response) &&
           response.command == request.command && response.request_id == request.request_id;
#endif
}

lvgl_platform::InboxScanResult InstallerClient::scan() noexcept
{
    lvgl_platform::InboxScanResult result;
    result.status = lvgl_platform::InboxStatus::io_error;
    result.detail = "INSTALLER_BROKER_UNAVAILABLE";
    if(next_request_id_ == std::numeric_limits<std::uint64_t>::max()) return result;
    lvgl_platform::InstallerResponse response;
    const lvgl_platform::InstallerRequest request {
        lvgl_platform::InstallerCommand::scan, next_request_id_++, 0, {}};
    if(!exchange(request, response)) return result;
    result.status = inbox_status(response.status);
    result.detail = response.detail;
    if(response.status == lvgl_platform::InstallerProtocolStatus::empty) return result;
    if(response.status != lvgl_platform::InstallerProtocolStatus::ready || response.count > 128) {
        return result;
    }
    result.candidates.reserve(response.count);
    for(std::uint32_t index = 0; index < response.count; ++index) {
        if(next_request_id_ == std::numeric_limits<std::uint64_t>::max()) {
            result.status = lvgl_platform::InboxStatus::io_error;
            result.detail = "INSTALLER_REQUEST_ID_EXHAUSTED";
            result.candidates.clear();
            return result;
        }
        lvgl_platform::InstallerResponse candidate;
        const lvgl_platform::InstallerRequest candidate_request {
            lvgl_platform::InstallerCommand::candidate, next_request_id_++, index, {}};
        if(!exchange(candidate_request, candidate) ||
           candidate.status != lvgl_platform::InstallerProtocolStatus::ready) {
            result.status = lvgl_platform::InboxStatus::io_error;
            result.detail = "INSTALLER_CANDIDATE_CHANGED";
            result.candidates.clear();
            return result;
        }
        result.candidates.push_back(std::move(candidate.candidate));
    }
    return result;
}

InstalledApplicationScanResult InstallerClient::scan_installed() noexcept
{
    InstalledApplicationScanResult result;
    result.detail = "INSTALLER_BROKER_UNAVAILABLE";
    if(next_request_id_ == std::numeric_limits<std::uint64_t>::max()) return result;
    lvgl_platform::InstallerResponse response;
    const lvgl_platform::InstallerRequest request {
        lvgl_platform::InstallerCommand::installed_scan, next_request_id_++, 0, {}};
    if(!exchange(request, response)) return result;
    result.detail = response.detail;
    if(response.status == lvgl_platform::InstallerProtocolStatus::empty) {
        result.success = true;
        return result;
    }
    if(response.status != lvgl_platform::InstallerProtocolStatus::ready || response.count > 64) {
        return result;
    }
    result.applications.reserve(response.count);
    for(std::uint32_t index = 0; index < response.count; ++index) {
        if(next_request_id_ == std::numeric_limits<std::uint64_t>::max()) {
            result.detail = "INSTALLER_REQUEST_ID_EXHAUSTED";
            result.applications.clear();
            return result;
        }
        lvgl_platform::InstallerResponse candidate;
        const lvgl_platform::InstallerRequest candidate_request {
            lvgl_platform::InstallerCommand::installed_candidate,
            next_request_id_++, index, {}};
        if(!exchange(candidate_request, candidate) ||
           candidate.status != lvgl_platform::InstallerProtocolStatus::ready) {
            result.detail = "INSTALLER_INSTALLED_SET_CHANGED";
            result.applications.clear();
            return result;
        }
        result.applications.push_back(std::move(candidate.installed));
    }
    result.success = true;
    return result;
}

InstallerClientResult InstallerClient::install(const std::string& token) noexcept
{
    return mutate(lvgl_platform::InstallerCommand::install, token);
}

InstallerClientResult InstallerClient::rollback(const std::string& token) noexcept
{
    return mutate(lvgl_platform::InstallerCommand::rollback, token);
}

InstallerClientResult InstallerClient::remove(const std::string& token) noexcept
{
    return mutate(lvgl_platform::InstallerCommand::remove, token);
}

InstallerClientResult InstallerClient::mutate(
    lvgl_platform::InstallerCommand command, const std::string& token) noexcept
{
    if(next_request_id_ == std::numeric_limits<std::uint64_t>::max()) {
        return {false, "INSTALLER_REQUEST_ID_EXHAUSTED"};
    }
    lvgl_platform::InstallerResponse response;
    const lvgl_platform::InstallerRequest request {
        command, next_request_id_++, 0, token};
    if(!exchange(request, response)) return {false, "INSTALLER_BROKER_UNAVAILABLE"};
    return {response.status == lvgl_platform::InstallerProtocolStatus::ready,
            std::move(response.detail)};
}

}  // namespace dictpen
