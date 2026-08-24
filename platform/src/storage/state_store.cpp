#include "lvgl_platform/state_store.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace lvgl_platform {
namespace {

StateStoreResult store_result(StateStoreStatus status, const char* detail)
{
    StateStoreResult result;
    result.status = status;
    result.detail = detail;
    return result;
}

bool safe_app_id(std::string_view value) noexcept
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

#if !defined(_WIN32)

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor()
    {
        if(value_ >= 0) ::close(value_);
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : value_(other.value_) { other.value_ = -1; }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if(this == &other) return *this;
        if(value_ >= 0) ::close(value_);
        value_ = other.value_;
        other.value_ = -1;
        return *this;
    }
    int get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ >= 0; }

private:
    int value_;
};

bool trusted_directory(int descriptor) noexcept
{
    struct stat status {};
    return ::fstat(descriptor, &status) == 0 && S_ISDIR(status.st_mode) &&
           status.st_uid == ::geteuid() && (status.st_mode & 0022) == 0;
}

FileDescriptor open_directory_at(int parent, const std::string& name) noexcept
{
    return FileDescriptor(::openat(
        parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
}

FileDescriptor ensure_directory_at(int parent, const std::string& name) noexcept
{
    bool created = false;
    if(::mkdirat(parent, name.c_str(), 0700) == 0) {
        created = true;
    } else if(errno != EEXIST) {
        return FileDescriptor();
    }
    auto directory = open_directory_at(parent, name);
    if(!directory.valid() || !trusted_directory(directory.get())) return FileDescriptor();
    if(created && (::fsync(directory.get()) != 0 || ::fsync(parent) != 0)) {
        return FileDescriptor();
    }
    return directory;
}

bool write_all(int descriptor, const std::vector<std::uint8_t>& bytes) noexcept
{
    std::size_t written = 0;
    while(written < bytes.size()) {
        const auto count = ::write(descriptor, bytes.data() + written, bytes.size() - written);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return false;
        written += static_cast<std::size_t>(count);
    }
    return true;
}

bool random_suffix(std::string& output) noexcept
{
    std::array<std::uint8_t, 16> random {};
    std::size_t received = 0;
    while(received < random.size()) {
        const auto count = ::getrandom(random.data() + received, random.size() - received, 0);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return false;
        received += static_cast<std::size_t>(count);
    }
    constexpr char hex[] = "0123456789abcdef";
    output.clear();
    output.reserve(random.size() * 2);
    for(const auto value : random) {
        output.push_back(hex[value >> 4U]);
        output.push_back(hex[value & 0x0fU]);
    }
    return true;
}

enum class SlotWrite { written, random_failed, io_failed };

SlotWrite write_slot(
    int application, const char* slot, const std::vector<std::uint8_t>& bytes) noexcept
{
    std::string suffix;
    if(!random_suffix(suffix)) return SlotWrite::random_failed;
    const std::string temporary = std::string(".") + slot + ".tmp-" + suffix;
    FileDescriptor output(::openat(
        application, temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0400));
    if(!output.valid() || !write_all(output.get(), bytes) || ::fsync(output.get()) != 0) {
        return SlotWrite::io_failed;
    }
    if(::renameat(application, temporary.c_str(), application, slot) != 0 ||
       ::fsync(application) != 0) {
        return SlotWrite::io_failed;
    }
    return SlotWrite::written;
}

enum class SlotRead { read, missing, invalid };

struct SlotContents {
    SlotRead status {SlotRead::invalid};
    std::vector<std::uint8_t> bytes;
};

SlotContents read_slot(int application, const char* slot)
{
    FileDescriptor input(::openat(application, slot, O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if(!input.valid()) return {errno == ENOENT ? SlotRead::missing : SlotRead::invalid, {}};
    struct stat status {};
    if(::fstat(input.get(), &status) != 0 || !S_ISREG(status.st_mode) ||
       status.st_uid != ::geteuid() || (status.st_mode & 0222) != 0 ||
       status.st_size <= 0 || status.st_size > 512) {
        return {SlotRead::invalid, {}};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(status.st_size));
    std::size_t received = 0;
    while(received < bytes.size()) {
        const auto count = ::read(input.get(), bytes.data() + received, bytes.size() - received);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return {SlotRead::invalid, {}};
        received += static_cast<std::size_t>(count);
    }
    std::uint8_t trailing = 0;
    if(::read(input.get(), &trailing, 1) != 0) return {SlotRead::invalid, {}};
    return {SlotRead::read, std::move(bytes)};
}

enum class OpenStoreStatus { opened, missing, untrusted };

struct StoreDirectories {
    OpenStoreStatus status {OpenStoreStatus::untrusted};
    FileDescriptor root;
    FileDescriptor apps;
    FileDescriptor application;
};

StoreDirectories open_store(
    const std::string& store_root, const std::string& app_id, bool create) noexcept
{
    if(store_root.empty() || store_root.front() != '/') return StoreDirectories();
    StoreDirectories directories;
    directories.root = FileDescriptor(::open(
        store_root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if(!directories.root.valid() || !trusted_directory(directories.root.get())) return directories;
    directories.apps = create ? ensure_directory_at(directories.root.get(), "apps")
                              : open_directory_at(directories.root.get(), "apps");
    if(!directories.apps.valid()) {
        directories.status = errno == ENOENT ? OpenStoreStatus::missing : OpenStoreStatus::untrusted;
        return directories;
    }
    if(!trusted_directory(directories.apps.get())) return directories;
    directories.application = create ? ensure_directory_at(directories.apps.get(), app_id)
                                     : open_directory_at(directories.apps.get(), app_id);
    if(!directories.application.valid()) {
        directories.status = errno == ENOENT ? OpenStoreStatus::missing : OpenStoreStatus::untrusted;
        return directories;
    }
    if(!trusted_directory(directories.application.get())) return directories;
    directories.status = OpenStoreStatus::opened;
    return directories;
}

#endif

}  // namespace

StateStoreResult persist_release_state(
    const std::string& store_root, const ApplicationReleaseState& state,
    const CryptoProvider& crypto)
{
    if(store_root.empty() || !safe_app_id(state.app_id)) {
        return store_result(StateStoreStatus::invalid_argument, "STATE_STORE_ARGUMENT_INVALID");
    }
    const auto encoded = encode_release_state(state, crypto);
    if(!encoded.ok()) {
        return store_result(StateStoreStatus::state_invalid, "STATE_STORE_STATE_INVALID");
    }
#if defined(_WIN32)
    return store_result(StateStoreStatus::unsupported_platform, "STATE_STORE_POSIX_REQUIRED");
#else
    auto directories = open_store(store_root, state.app_id, true);
    if(directories.status != OpenStoreStatus::opened) {
        return store_result(StateStoreStatus::root_untrusted, "STATE_STORE_PATH_UNTRUSTED");
    }
    const char* first = state.generation % 2 == 0 ? "state.b" : "state.a";
    const char* second = state.generation % 2 == 0 ? "state.a" : "state.b";
    const auto first_write = write_slot(
        directories.application.get(), first, encoded.bytes);
    if(first_write == SlotWrite::random_failed) {
        return store_result(StateStoreStatus::random_unavailable, "STATE_STORE_RANDOM_UNAVAILABLE");
    }
    if(first_write != SlotWrite::written) {
        return store_result(StateStoreStatus::io_error, "STATE_STORE_FIRST_SLOT_FAILED");
    }
    const auto second_write = write_slot(
        directories.application.get(), second, encoded.bytes);
    if(::fsync(directories.apps.get()) != 0 || ::fsync(directories.root.get()) != 0) {
        return store_result(StateStoreStatus::written_degraded, "STATE_STORE_PARENT_SYNC_DEGRADED");
    }
    if(second_write != SlotWrite::written) {
        return store_result(StateStoreStatus::written_degraded, "STATE_STORE_SECOND_SLOT_DEGRADED");
    }
    return store_result(StateStoreStatus::written, "STATE_STORE_WRITTEN");
#endif
}

StateStoreResult load_release_state(
    const std::string& store_root, const std::string& app_id,
    const CryptoProvider& crypto)
{
    if(store_root.empty() || !safe_app_id(app_id)) {
        return store_result(StateStoreStatus::invalid_argument, "STATE_STORE_ARGUMENT_INVALID");
    }
#if defined(_WIN32)
    (void)crypto;
    return store_result(StateStoreStatus::unsupported_platform, "STATE_STORE_POSIX_REQUIRED");
#else
    auto directories = open_store(store_root, app_id, false);
    if(directories.status == OpenStoreStatus::missing) {
        return store_result(StateStoreStatus::not_found, "STATE_STORE_NOT_FOUND");
    }
    if(directories.status != OpenStoreStatus::opened) {
        return store_result(StateStoreStatus::root_untrusted, "STATE_STORE_PATH_UNTRUSTED");
    }
    auto slot_a = read_slot(directories.application.get(), "state.a");
    auto slot_b = read_slot(directories.application.get(), "state.b");
    if(slot_a.status == SlotRead::missing && slot_b.status == SlotRead::missing) {
        return store_result(StateStoreStatus::uninitialized, "STATE_STORE_UNINITIALIZED");
    }
    auto selected = select_release_state(slot_a.bytes, slot_b.bytes, crypto);
    if(!selected.ok() || selected.state.app_id != app_id) {
        auto failure = store_result(StateStoreStatus::state_invalid, "STATE_STORE_INVALID");
        failure.selected = std::move(selected);
        return failure;
    }
    StateStoreResult result;
    result.status = selected.redundancy_degraded ? StateStoreStatus::loaded_degraded
                                                 : StateStoreStatus::loaded;
    result.detail = selected.redundancy_degraded ? "STATE_STORE_LOADED_DEGRADED"
                                                  : "STATE_STORE_LOADED";
    result.selected = std::move(selected);
    return result;
#endif
}

}  // namespace lvgl_platform
