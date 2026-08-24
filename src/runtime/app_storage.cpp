#include "runtime/app_storage.h"

#include "lvgl_platform/storage_protocol.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace dictpen {
namespace {

#if !defined(_WIN32)

int inherited_storage_socket() noexcept
{
    const char* value = std::getenv("LVGL_APP_STORAGE_FD");
    if(value == nullptr || *value == '\0') return -1;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if(errno != 0 || end == value || *end != '\0' || parsed < 3 ||
       parsed > std::numeric_limits<int>::max()) {
        return -1;
    }
    const int descriptor = static_cast<int>(parsed);
    struct stat details {};
    int type = 0;
    socklen_t type_size = sizeof(type);
    if(::fstat(descriptor, &details) != 0 || !S_ISSOCK(details.st_mode) ||
       ::getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &type_size) != 0 ||
       type != SOCK_SEQPACKET) {
        return -1;
    }
#if defined(SO_PEERCRED)
    ucred peer {};
    socklen_t peer_size = sizeof(peer);
    if(::getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0 ||
       peer_size != sizeof(peer) || peer.uid != 0) {
        return -1;
    }
#endif
    const int duplicate = ::fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
    if(duplicate < 0) return -1;
    const timeval timeout {2, 0};
    if(::setsockopt(duplicate, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
       ::setsockopt(duplicate, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        ::close(duplicate);
        return -1;
    }
    return duplicate;
}

bool exchange(
    int descriptor, const lvgl_platform::StorageRequest& request,
    lvgl_platform::StorageResponse& response) noexcept
{
    const auto packet = lvgl_platform::encode_storage_request(request);
    if(packet.empty() ||
       ::send(descriptor, packet.data(), packet.size(), MSG_NOSIGNAL) !=
           static_cast<ssize_t>(packet.size())) {
        return false;
    }
    std::array<std::uint8_t, lvgl_platform::kMaximumStoragePacketSize> bytes {};
    const auto received = ::recv(descriptor, bytes.data(), bytes.size(), MSG_TRUNC);
    return received >= 0 && static_cast<std::size_t>(received) <= bytes.size() &&
           lvgl_platform::decode_storage_response(
               bytes.data(), static_cast<std::size_t>(received), response) &&
           response.request_id == request.request_id && response.command == request.command;
}

#endif

}  // namespace

AppStorage::AppStorage() noexcept
{
#if !defined(_WIN32)
    socket_fd_ = inherited_storage_socket();
#endif
}

AppStorage::~AppStorage()
{
#if !defined(_WIN32)
    if(socket_fd_ >= 0) ::close(socket_fd_);
#endif
}

bool AppStorage::read(
    std::string_view record, std::vector<std::uint8_t>& output,
    std::size_t maximum_size) const noexcept
{
    output.clear();
#if defined(_WIN32)
    (void)record;
    (void)maximum_size;
    return false;
#else
    if(socket_fd_ < 0 || maximum_size == 0 ||
       maximum_size > lvgl_platform::kMaximumStorageRecordSize ||
       !lvgl_platform::valid_storage_record_name(record)) {
        return false;
    }
    const auto request_id = next_request_id_++;
    if(request_id == 0) return false;
    const lvgl_platform::StorageRequest request {
        lvgl_platform::StorageCommand::read, request_id, std::string(record),
        static_cast<std::uint32_t>(maximum_size), {}};
    lvgl_platform::StorageResponse response;
    if(!exchange(socket_fd_, request, response) ||
       response.status != lvgl_platform::StorageProtocolStatus::ok || response.data.empty() ||
       response.data.size() > maximum_size) {
        return false;
    }
    output = std::move(response.data);
    return true;
#endif
}

bool AppStorage::write_atomic(
    std::string_view record, const void* data, std::size_t size,
    std::size_t maximum_size) const noexcept
{
#if defined(_WIN32)
    (void)record;
    (void)data;
    (void)size;
    (void)maximum_size;
    return false;
#else
    if(socket_fd_ < 0 || data == nullptr || size == 0 || size > maximum_size ||
       maximum_size > lvgl_platform::kMaximumStorageRecordSize ||
       !lvgl_platform::valid_storage_record_name(record)) {
        return false;
    }
    const auto request_id = next_request_id_++;
    if(request_id == 0) return false;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const lvgl_platform::StorageRequest request {
        lvgl_platform::StorageCommand::write, request_id, std::string(record),
        static_cast<std::uint32_t>(maximum_size),
        std::vector<std::uint8_t>(bytes, bytes + size)};
    lvgl_platform::StorageResponse response;
    return exchange(socket_fd_, request, response) &&
           response.status == lvgl_platform::StorageProtocolStatus::ok && response.data.empty();
#endif
}

}  // namespace dictpen
