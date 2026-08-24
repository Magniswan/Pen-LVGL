#include "lvgl_platform/safe_path.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace lvgl_platform {
namespace {

StorageResult result(StorageStatus status, const char* detail, std::string path = {})
{
    return {status, detail, std::move(path)};
}

bool safe_component(std::string_view value) noexcept
{
    if(value.empty() || value.size() > 128 || value == "." || value == "..") return false;
    return std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '.' || character == '-';
    });
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

FileDescriptor ensure_directory_at(int parent, const std::string& name, mode_t mode) noexcept
{
    bool created = false;
    if(::mkdirat(parent, name.c_str(), mode) == 0) {
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

bool write_all(int descriptor, const std::uint8_t* bytes, std::size_t size) noexcept
{
    std::size_t written = 0;
    while(written < size) {
        const auto remaining = size - written;
        const auto chunk = std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const auto count = ::write(descriptor, bytes + written, chunk);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return false;
        written += static_cast<std::size_t>(count);
    }
    return true;
}

std::vector<std::string> split_path(const std::string& path)
{
    std::vector<std::string> components;
    std::size_t start = 0;
    while(start < path.size()) {
        const auto end = path.find('/', start);
        components.emplace_back(path.substr(
            start, end == std::string::npos ? path.size() - start : end - start));
        if(end == std::string::npos) break;
        start = end + 1;
    }
    return components;
}

bool create_file(
    int release, const VerifiedPackageFile& file, const std::uint8_t* payload) noexcept
{
    const auto components = split_path(file.path);
    if(components.empty()) return false;
    FileDescriptor parent(::dup(release));
    if(!parent.valid()) return false;
    for(std::size_t index = 0; index + 1 < components.size(); ++index) {
        auto child = ensure_directory_at(parent.get(), components[index], 0755);
        if(!child.valid()) return false;
        parent = std::move(child);
    }
    FileDescriptor output(::openat(
        parent.get(), components.back().c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if(!output.valid() || !write_all(output.get(), payload, static_cast<std::size_t>(file.size)) ||
       ::fchmod(output.get(), static_cast<mode_t>(file.mode)) != 0 || ::fsync(output.get()) != 0) {
        return false;
    }
    return ::fsync(parent.get()) == 0;
}

bool create_package_copy(
    int release, const std::uint8_t* bytes, std::size_t size,
    const Sha512Digest& digest) noexcept
{
    FileDescriptor package(::openat(
        release, ".package.lvapp", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
        0400));
    if(!package.valid() || !write_all(package.get(), bytes, size) || ::fsync(package.get()) != 0) {
        return false;
    }
    FileDescriptor marker(::openat(
        release, ".package.sha512", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
        0400));
    if(!marker.valid() || !write_all(marker.get(), digest.data(), digest.size()) ||
       ::fsync(marker.get()) != 0) {
        return false;
    }
    return ::fsync(release) == 0;
}

bool release_package_matches(
    int releases, const std::string& release, const std::uint8_t* expected_bytes,
    std::size_t expected_size, const Sha512Digest& expected_digest)
{
    auto directory = open_directory_at(releases, release);
    if(!directory.valid() || !trusted_directory(directory.get())) return false;
    FileDescriptor package(::openat(
        directory.get(), ".package.lvapp", O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat package_status {};
    if(!package.valid() || ::fstat(package.get(), &package_status) != 0 ||
       !S_ISREG(package_status.st_mode) || package_status.st_uid != ::geteuid() ||
       (package_status.st_mode & 0222) != 0 || package_status.st_size < 0 ||
       static_cast<std::uint64_t>(package_status.st_size) != expected_size) {
        return false;
    }
    std::array<std::uint8_t, 64U * 1024U> buffer {};
    std::size_t compared = 0;
    while(compared < expected_size) {
        const auto wanted = std::min(buffer.size(), expected_size - compared);
        const auto count = ::read(package.get(), buffer.data(), wanted);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0 || static_cast<std::size_t>(count) > wanted ||
           std::memcmp(buffer.data(), expected_bytes + compared, static_cast<std::size_t>(count)) !=
               0) {
            return false;
        }
        compared += static_cast<std::size_t>(count);
    }
    FileDescriptor marker(::openat(
        directory.get(), ".package.sha512", O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if(!marker.valid()) return false;
    Sha512Digest actual {};
    std::size_t received = 0;
    while(received < actual.size()) {
        const auto count = ::read(marker.get(), actual.data() + received, actual.size() - received);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return false;
        received += static_cast<std::size_t>(count);
    }
    std::uint8_t trailing = 0;
    const auto trailing_count = ::read(marker.get(), &trailing, 1);
    return trailing_count == 0 && actual == expected_digest;
}

bool random_stage_name(std::uint64_t counter, std::string& output) noexcept
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
    output = ".stage-" + std::to_string(counter) + '-';
    for(const auto value : random) {
        output.push_back(hex[value >> 4U]);
        output.push_back(hex[value & 0x0fU]);
    }
    return true;
}

int rename_no_replace(
    int old_parent, const char* old_name, int new_parent, const char* new_name) noexcept
{
#if defined(SYS_renameat2)
    constexpr unsigned int no_replace = 1;
    return static_cast<int>(
        ::syscall(SYS_renameat2, old_parent, old_name, new_parent, new_name, no_replace));
#else
    (void)old_parent;
    (void)old_name;
    (void)new_parent;
    (void)new_name;
    errno = ENOSYS;
    return -1;
#endif
}

#endif

}  // namespace

StorageResult stage_verified_release(
    const std::string& store_root, const std::uint8_t* package_bytes,
    std::size_t package_size, const PackageVerification& verification,
    const Sha512Digest& package_digest)
{
    if(store_root.empty() || package_bytes == nullptr || package_size == 0 ||
       !safe_component(verification.manifest.app_id) ||
       verification.manifest.release_counter == 0) {
        return result(StorageStatus::invalid_argument, "STORAGE_ARGUMENT_INVALID");
    }
    if(!verification.ok() || verification.development || !verification.signature_verified) {
        return result(StorageStatus::package_not_authenticated, "STORAGE_PACKAGE_NOT_AUTHENTICATED");
    }
    if(verification.payload_start > package_size) {
        return result(StorageStatus::invalid_argument, "STORAGE_PACKAGE_LAYOUT_INVALID");
    }

#if defined(_WIN32)
    (void)package_digest;
    return result(StorageStatus::unsupported_platform, "STORAGE_POSIX_REQUIRED");
#else
    if(store_root.front() != '/') {
        return result(StorageStatus::invalid_argument, "STORAGE_ROOT_MUST_BE_ABSOLUTE");
    }
    FileDescriptor root(::open(
        store_root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if(!root.valid() || !trusted_directory(root.get())) {
        return result(StorageStatus::root_untrusted, "STORAGE_ROOT_UNTRUSTED");
    }
    auto apps = ensure_directory_at(root.get(), "apps", 0700);
    auto application = apps.valid()
                           ? ensure_directory_at(apps.get(), verification.manifest.app_id, 0700)
                           : FileDescriptor();
    auto releases = application.valid()
                        ? ensure_directory_at(application.get(), "releases", 0700)
                        : FileDescriptor();
    if(!apps.valid() || !application.valid() || !releases.valid()) {
        return result(StorageStatus::path_rejected, "STORAGE_DIRECTORY_REJECTED");
    }

    const auto release_name = std::to_string(verification.manifest.release_counter);
    errno = 0;
    auto existing_release = open_directory_at(releases.get(), release_name);
    if(existing_release.valid()) {
        if(release_package_matches(
               releases.get(), release_name, package_bytes, package_size, package_digest)) {
            return result(
                StorageStatus::already_present, "STORAGE_RELEASE_ALREADY_PRESENT",
                store_root + "/apps/" + verification.manifest.app_id + "/releases/" +
                    release_name);
        }
        return result(StorageStatus::release_collision, "STORAGE_RELEASE_COLLISION");
    }
    if(errno != ENOENT) {
        return result(StorageStatus::path_rejected, "STORAGE_RELEASE_PATH_REJECTED");
    }

    std::string stage_name;
    if(!random_stage_name(verification.manifest.release_counter, stage_name)) {
        return result(StorageStatus::random_unavailable, "STORAGE_RANDOM_UNAVAILABLE");
    }
    if(::mkdirat(releases.get(), stage_name.c_str(), 0700) != 0) {
        return result(StorageStatus::io_error, "STORAGE_STAGE_CREATE_FAILED");
    }
    auto stage = open_directory_at(releases.get(), stage_name);
    if(!stage.valid() || !trusted_directory(stage.get())) {
        return result(StorageStatus::path_rejected, "STORAGE_STAGE_REJECTED");
    }
    for(const auto& file : verification.manifest.files) {
        if(file.payload_offset > package_size - verification.payload_start ||
           file.size > package_size - verification.payload_start - file.payload_offset ||
           file.size > std::numeric_limits<std::size_t>::max()) {
            return result(StorageStatus::invalid_argument, "STORAGE_FILE_RANGE_INVALID");
        }
        if(!create_file(
               stage.get(), file,
               package_bytes + verification.payload_start + file.payload_offset)) {
            return result(StorageStatus::io_error, "STORAGE_FILE_WRITE_FAILED");
        }
    }
    if(!create_package_copy(stage.get(), package_bytes, package_size, package_digest)) {
        return result(StorageStatus::io_error, "STORAGE_METADATA_WRITE_FAILED");
    }

    if(rename_no_replace(
           releases.get(), stage_name.c_str(), releases.get(), release_name.c_str()) != 0) {
        if(errno == EEXIST) {
            return result(StorageStatus::release_collision, "STORAGE_RELEASE_ALREADY_EXISTS");
        }
        return result(StorageStatus::io_error, "STORAGE_RELEASE_COMMIT_FAILED");
    }
    if(::fsync(releases.get()) != 0 || ::fsync(application.get()) != 0 ||
       ::fsync(apps.get()) != 0 || ::fsync(root.get()) != 0) {
        return result(StorageStatus::io_error, "STORAGE_DIRECTORY_SYNC_FAILED");
    }
    return result(
        StorageStatus::committed, "STORAGE_RELEASE_COMMITTED",
        store_root + "/apps/" + verification.manifest.app_id + "/releases/" + release_name);
#endif
}

}  // namespace lvgl_platform
