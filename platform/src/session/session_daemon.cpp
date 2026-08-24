#include "lvgl_platform/application_registry.h"
#include "lvgl_platform/crypto_provider.h"
#include "lvgl_platform/inbox_service.h"
#include "lvgl_platform/installer_protocol.h"
#include "lvgl_platform/rollback_policy.h"
#include "lvgl_platform/session_control.h"
#include "lvgl_platform/session_profile.h"
#include "lvgl_platform/session_status.h"
#include "lvgl_platform/storage_protocol.h"
#include "lvgl_platform/touch_protocol.h"
#include "lvgl_platform/touch_router.h"
#include "lvgl_platform/trust_store.h"
#include "lvgl_platform/state_store.h"
#include "lvgl_platform/version.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <memory>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr const char* kPlatformRoot = "/userdisk/apps/lvgl-platform";
constexpr const char* kReleasePrefix =
    "/userdisk/apps/lvgl-platform/apps/top.lvgl.platform/releases/";
constexpr const char* kTouchPath = "/run/lvgl-platform/touch.sock";
constexpr const char* kApplicationStore = "/userdisk/apps/lvgl-apps";
constexpr const char* kApplicationPolicyStore = "/userdisk/apps/lvgl-app-policy";
constexpr const char* kApplicationDataDirectory = "lvgl-data";
constexpr std::size_t kMaximumPackageSize = 256U * 1024U * 1024U + 96U;
constexpr int kReadyTimeoutMilliseconds = 10000;

volatile std::sig_atomic_t g_stop = 0;

void signal_handler(int) { g_stop = 1; }

class FileDescriptor {
public:
    FileDescriptor() noexcept = default;
    explicit FileDescriptor(int value) noexcept : value_(value) {}
    ~FileDescriptor() { reset(); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : value_(other.release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if(this != &other) reset(other.release());
        return *this;
    }
    int get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ >= 0; }
    int release() noexcept
    {
        const int value = value_;
        value_ = -1;
        return value;
    }
    void reset(int value = -1) noexcept
    {
        if(value_ >= 0) ::close(value_);
        value_ = value;
    }

private:
    int value_ {-1};
};

struct VerifiedRelease {
    FileDescriptor directory;
    lvgl_platform::PackageVerification verification;
    lvgl_platform::CertifiedSessionProfile profile;
    std::uint64_t counter {0};
};

struct VerifiedApplication {
    FileDescriptor release;
    lvgl_platform::PackageVerification verification;
    std::vector<std::uint8_t> package;
};

struct ProgramPolicy {
    lvgl_platform::ApplicationResourceLimits limits;
    bool private_storage {false};
    uid_t uid {0};
    gid_t gid {0};
};

struct DirectoryCloser {
    void operator()(DIR* directory) const noexcept
    {
        if(directory != nullptr) ::closedir(directory);
    }
};

bool trusted_directory(int descriptor) noexcept
{
    struct stat details {};
    return descriptor >= 0 && ::fstat(descriptor, &details) == 0 && S_ISDIR(details.st_mode) &&
           details.st_uid == 0 && (details.st_mode & 0022) == 0;
}

bool private_directory(int descriptor) noexcept
{
    struct stat details {};
    return descriptor >= 0 && ::fstat(descriptor, &details) == 0 && S_ISDIR(details.st_mode) &&
           details.st_uid == 0 && (details.st_mode & 0777) == 0700;
}

uid_t application_uid(std::string_view app_id) noexcept
{
    std::uint32_t hash = 2166136261U;
    for(const unsigned char character : app_id) {
        hash ^= character;
        hash *= 16777619U;
    }
    constexpr std::uint32_t kFirstApplicationUid = 100000U;
    constexpr std::uint32_t kApplicationUidRange = 900000000U;
    return static_cast<uid_t>(kFirstApplicationUid + hash % kApplicationUidRange);
}

bool trusted_regular(int descriptor, std::uint16_t expected_mode, std::uint64_t expected_size) noexcept
{
    struct stat details {};
    return descriptor >= 0 && ::fstat(descriptor, &details) == 0 && S_ISREG(details.st_mode) &&
           details.st_uid == 0 && details.st_nlink == 1 && details.st_size >= 0 &&
           static_cast<std::uint64_t>(details.st_size) == expected_size &&
           static_cast<std::uint16_t>(details.st_mode & 0777) == expected_mode;
}

std::vector<std::string> split_path(std::string_view path)
{
    std::vector<std::string> components;
    std::size_t start = path.front() == '/' ? 1 : 0;
    while(start < path.size()) {
        const auto end = path.find('/', start);
        const auto length = end == std::string_view::npos ? path.size() - start : end - start;
        if(length == 0) return {};
        const auto part = path.substr(start, length);
        if(part == "." || part == "..") return {};
        components.emplace_back(part);
        if(end == std::string_view::npos) break;
        start = end + 1;
    }
    return components;
}

FileDescriptor open_absolute_directory(std::string_view path) noexcept
{
    if(path.empty() || path.front() != '/') return {};
    FileDescriptor current(::open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    for(const auto& component : split_path(path)) {
        if(!trusted_directory(current.get())) return {};
        FileDescriptor next(::openat(
            current.get(), component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if(!trusted_directory(next.get())) return {};
        current = std::move(next);
    }
    return current;
}

FileDescriptor ensure_private_storage(std::string_view app_id) noexcept
{
    if(!lvgl_platform::valid_session_app_id(app_id)) return {};
    auto apps = open_absolute_directory("/userdisk/apps");
    if(!trusted_directory(apps.get())) return {};
    bool root_created = false;
    if(::mkdirat(apps.get(), kApplicationDataDirectory, 0700) == 0) root_created = true;
    else if(errno != EEXIST) return {};
    FileDescriptor root(::openat(
        apps.get(), kApplicationDataDirectory,
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if(!private_directory(root.get()) || (root_created && ::fsync(apps.get()) != 0)) return {};

    const std::string name(app_id);
    bool app_created = false;
    if(::mkdirat(root.get(), name.c_str(), 0700) == 0) app_created = true;
    else if(errno != EEXIST) return {};
    FileDescriptor directory(::openat(
        root.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if(!private_directory(directory.get()) ||
       (app_created && (::fsync(directory.get()) != 0 || ::fsync(root.get()) != 0))) {
        return {};
    }
    return directory;
}

bool write_all(int descriptor, const void* data, std::size_t size) noexcept;

struct StorageUsage {
    std::uint32_t files {0};
    std::uint64_t bytes {0};
};

bool storage_temporary_name(std::string_view name) noexcept
{
    constexpr std::string_view broker_prefix = ".broker-";
    constexpr std::string_view legacy_prefix = ".write-";
    const auto prefix = name.substr(0, broker_prefix.size()) == broker_prefix
                            ? broker_prefix
                            : legacy_prefix;
    if(name.size() != prefix.size() + 24 || name.substr(0, prefix.size()) != prefix) return false;
    return std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefix.size()), name.end(),
                       [](const char character) {
                           return (character >= '0' && character <= '9') ||
                                  (character >= 'a' && character <= 'f');
                       });
}

bool scan_storage(int directory, StorageUsage& usage) noexcept
{
    usage = {};
    bool cleaned = false;
    const int duplicate = ::dup(directory);
    if(duplicate < 0) return false;
    std::unique_ptr<DIR, DirectoryCloser> entries(::fdopendir(duplicate));
    if(!entries) {
        ::close(duplicate);
        return false;
    }
    while(const auto* entry = ::readdir(entries.get())) {
        const std::string_view name(entry->d_name);
        if(name == "." || name == "..") continue;
        struct stat details {};
        if(::fstatat(directory, entry->d_name, &details, AT_SYMLINK_NOFOLLOW) != 0 ||
           !S_ISREG(details.st_mode) || details.st_uid != 0 || details.st_nlink != 1 ||
           (details.st_mode & 0777) != 0600) {
            return false;
        }
        if(storage_temporary_name(name)) {
            if(::unlinkat(directory, entry->d_name, 0) != 0) return false;
            cleaned = true;
            continue;
        }
        if(details.st_size <= 0 || !lvgl_platform::valid_storage_record_name(name) ||
           static_cast<std::uint64_t>(details.st_size) >
               lvgl_platform::kMaximumStorageRecordSize ||
           usage.files == std::numeric_limits<std::uint32_t>::max() ||
           usage.bytes > std::numeric_limits<std::uint64_t>::max() -
                             static_cast<std::uint64_t>(details.st_size)) {
            return false;
        }
        ++usage.files;
        usage.bytes += static_cast<std::uint64_t>(details.st_size);
    }
    return !cleaned || ::fsync(directory) == 0;
}

bool storage_random_suffix(std::string& output) noexcept
{
    std::array<std::uint8_t, 12> random {};
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

lvgl_platform::StorageProtocolStatus read_storage_record(
    int directory, const lvgl_platform::StorageRequest& request,
    std::vector<std::uint8_t>& data) noexcept
{
    const std::string name(request.record);
    FileDescriptor input(::openat(
        directory, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if(!input.valid()) {
        return errno == ENOENT ? lvgl_platform::StorageProtocolStatus::not_found
                               : lvgl_platform::StorageProtocolStatus::io_error;
    }
    struct stat details {};
    if(::fstat(input.get(), &details) != 0 || !S_ISREG(details.st_mode) ||
       details.st_uid != 0 || details.st_nlink != 1 || (details.st_mode & 0777) != 0600 ||
       details.st_size <= 0 ||
       static_cast<std::uint64_t>(details.st_size) > request.maximum_size) {
        return lvgl_platform::StorageProtocolStatus::storage_corrupt;
    }
    data.assign(static_cast<std::size_t>(details.st_size), 0);
    std::size_t received = 0;
    while(received < data.size()) {
        const auto count = ::read(input.get(), data.data() + received, data.size() - received);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return lvgl_platform::StorageProtocolStatus::io_error;
        received += static_cast<std::size_t>(count);
    }
    std::uint8_t trailing = 0;
    return ::read(input.get(), &trailing, 1) == 0
               ? lvgl_platform::StorageProtocolStatus::ok
               : lvgl_platform::StorageProtocolStatus::storage_corrupt;
}

lvgl_platform::StorageProtocolStatus write_storage_record(
    int directory, const lvgl_platform::StorageRequest& request,
    const lvgl_platform::ApplicationResourceLimits& limits) noexcept
{
    StorageUsage usage;
    if(!scan_storage(directory, usage)) return lvgl_platform::StorageProtocolStatus::storage_corrupt;
    const std::string name(request.record);
    struct stat existing {};
    std::uint64_t existing_size = 0;
    bool exists = false;
    if(::fstatat(directory, name.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        if(!S_ISREG(existing.st_mode) || existing.st_uid != 0 || existing.st_nlink != 1 ||
           (existing.st_mode & 0777) != 0600 || existing.st_size <= 0) {
            return lvgl_platform::StorageProtocolStatus::storage_corrupt;
        }
        exists = true;
        existing_size = static_cast<std::uint64_t>(existing.st_size);
    } else if(errno != ENOENT) {
        return lvgl_platform::StorageProtocolStatus::io_error;
    }
    const std::uint64_t quota = static_cast<std::uint64_t>(limits.data_mib) * 1024U * 1024U;
    if(!lvgl_platform::storage_write_within_quota(
           usage.files, usage.bytes, exists, existing_size, request.data.size(),
           limits.maximum_files, quota)) {
        return lvgl_platform::StorageProtocolStatus::quota_exceeded;
    }
    std::string suffix;
    if(!storage_random_suffix(suffix)) return lvgl_platform::StorageProtocolStatus::io_error;
    const std::string temporary = ".broker-" + suffix;
    FileDescriptor output(::openat(
        directory, temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if(!output.valid() || !write_all(output.get(), request.data.data(), request.data.size()) ||
       ::fsync(output.get()) != 0 ||
       ::renameat(directory, temporary.c_str(), directory, name.c_str()) != 0 ||
       ::fsync(directory) != 0) {
        ::unlinkat(directory, temporary.c_str(), 0);
        return lvgl_platform::StorageProtocolStatus::io_error;
    }
    return lvgl_platform::StorageProtocolStatus::ok;
}

bool handle_storage_request(
    int socket_descriptor, int directory,
    const lvgl_platform::ApplicationResourceLimits& limits) noexcept
{
    std::array<std::uint8_t, lvgl_platform::kMaximumStoragePacketSize> bytes {};
    const auto received = ::recv(
        socket_descriptor, bytes.data(), bytes.size(), MSG_DONTWAIT | MSG_TRUNC);
    if(received <= 0 || static_cast<std::size_t>(received) > bytes.size()) return false;
    lvgl_platform::StorageRequest request;
    if(!lvgl_platform::decode_storage_request(
           bytes.data(), static_cast<std::size_t>(received), request)) {
        return false;
    }
    lvgl_platform::StorageResponse response {
        request.command, lvgl_platform::StorageProtocolStatus::invalid_request, request.request_id, {}};
    if(request.command == lvgl_platform::StorageCommand::read) {
        StorageUsage usage;
        if(!scan_storage(directory, usage) || usage.files > limits.maximum_files ||
           usage.bytes > static_cast<std::uint64_t>(limits.data_mib) * 1024U * 1024U) {
            response.status = lvgl_platform::StorageProtocolStatus::storage_corrupt;
        } else {
            response.status = read_storage_record(directory, request, response.data);
            if(response.status != lvgl_platform::StorageProtocolStatus::ok) response.data.clear();
        }
    } else if(request.command == lvgl_platform::StorageCommand::write) {
        response.status = write_storage_record(directory, request, limits);
    }
    const auto encoded = lvgl_platform::encode_storage_response(response);
    return !encoded.empty() &&
           ::send(socket_descriptor, encoded.data(), encoded.size(), MSG_NOSIGNAL) ==
               static_cast<ssize_t>(encoded.size());
}

FileDescriptor open_release_file(int release, std::string_view path) noexcept
{
    const auto components = split_path(path);
    if(components.empty()) return {};
    FileDescriptor parent(::dup(release));
    for(std::size_t index = 0; index + 1 < components.size(); ++index) {
        FileDescriptor child(::openat(
            parent.get(), components[index].c_str(),
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if(!trusted_directory(child.get())) return {};
        parent = std::move(child);
    }
    return FileDescriptor(::openat(
        parent.get(), components.back().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
}

bool read_all(int descriptor, std::vector<std::uint8_t>& output, std::size_t maximum)
{
    struct stat details {};
    if(descriptor < 0 || ::fstat(descriptor, &details) != 0 || !S_ISREG(details.st_mode) ||
       details.st_uid != 0 || details.st_nlink != 1 || (details.st_mode & 0222) != 0 ||
       details.st_size <= 0 ||
       static_cast<std::uint64_t>(details.st_size) > maximum) {
        return false;
    }
    output.assign(static_cast<std::size_t>(details.st_size), 0);
    std::size_t received = 0;
    while(received < output.size()) {
        const auto count = ::read(descriptor, output.data() + received, output.size() - received);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return false;
        received += static_cast<std::size_t>(count);
    }
    std::uint8_t trailing = 0;
    return ::read(descriptor, &trailing, 1) == 0;
}

bool bytes_match(int descriptor, const std::uint8_t* expected, std::size_t size) noexcept
{
    std::array<std::uint8_t, 64U * 1024U> buffer {};
    std::size_t compared = 0;
    while(compared < size) {
        const auto wanted = std::min(buffer.size(), size - compared);
        const auto count = ::read(descriptor, buffer.data(), wanted);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0 || static_cast<std::size_t>(count) > wanted ||
           std::memcmp(buffer.data(), expected + compared, static_cast<std::size_t>(count)) != 0) {
            return false;
        }
        compared += static_cast<std::size_t>(count);
    }
    std::uint8_t trailing = 0;
    return ::read(descriptor, &trailing, 1) == 0;
}

bool active_link_matches(std::uint64_t counter) noexcept
{
    std::array<char, 256> target {};
    const std::string link = std::string(kPlatformRoot) + "/current";
    const auto count = ::readlink(link.c_str(), target.data(), target.size() - 1);
    const std::string expected =
        "apps/top.lvgl.platform/releases/" + std::to_string(counter);
    return count == static_cast<ssize_t>(expected.size()) &&
           std::memcmp(target.data(), expected.data(), expected.size()) == 0;
}

bool resolve_self_release(std::string& release_path, std::uint64_t& counter) noexcept
{
    std::array<char, 1024> path {};
    const auto count = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
    if(count <= 0 || static_cast<std::size_t>(count) >= path.size()) return false;
    const std::string_view self(path.data(), static_cast<std::size_t>(count));
    const std::string_view prefix(kReleasePrefix);
    constexpr std::string_view suffix = "/bin/lvgl-sessiond";
    if(self.size() <= prefix.size() + suffix.size() || self.substr(0, prefix.size()) != prefix ||
       self.substr(self.size() - suffix.size()) != suffix) {
        return false;
    }
    const auto release = self.substr(prefix.size(), self.size() - prefix.size() - suffix.size());
    const auto conversion = std::from_chars(
        release.data(), release.data() + release.size(), counter, 10);
    if(release.empty() || release.front() == '0' || conversion.ec != std::errc() ||
       conversion.ptr != release.data() + release.size() || counter == 0) {
        return false;
    }
    release_path.assign(self.substr(0, self.size() - suffix.size()));
    return active_link_matches(counter);
}

bool installed_files_match(
    int release, const std::vector<std::uint8_t>& package,
    const lvgl_platform::PackageVerification& verification) noexcept
{
    if(verification.payload_start > package.size()) return false;
    for(const auto& file : verification.manifest.files) {
        if(file.payload_offset > package.size() - verification.payload_start ||
           file.size > package.size() - verification.payload_start - file.payload_offset ||
           file.size > std::numeric_limits<std::size_t>::max()) {
            return false;
        }
        auto installed = open_release_file(release, file.path);
        if(!trusted_regular(installed.get(), file.mode, file.size) ||
           !bytes_match(installed.get(),
                        package.data() + verification.payload_start + file.payload_offset,
                        static_cast<std::size_t>(file.size))) {
            return false;
        }
    }
    return true;
}

bool verify_active_release(VerifiedRelease& output, std::string& error)
{
    std::string release_path;
    if(!resolve_self_release(release_path, output.counter)) {
        error = "SESSION_RELEASE_PATH_REJECTED";
        return false;
    }
    output.directory = open_absolute_directory(release_path);
    if(!trusted_directory(output.directory.get())) {
        error = "SESSION_RELEASE_DIRECTORY_UNTRUSTED";
        return false;
    }
    auto package_file = open_release_file(output.directory.get(), ".package.lvapp");
    std::vector<std::uint8_t> package;
    if(!read_all(package_file.get(), package, kMaximumPackageSize)) {
        error = "SESSION_PACKAGE_UNREADABLE";
        return false;
    }
    const auto trust = lvgl_platform::OfficialTrustStore::compiled();
    const auto crypto = lvgl_platform::CryptoProvider::load_default();
    if(!trust.configured() || crypto == nullptr) {
        error = "SESSION_OFFICIAL_TRUST_UNAVAILABLE";
        return false;
    }
    output.verification = trust.verify(package.data(), package.size(), *crypto);
    if(!output.verification.ok() || output.verification.development ||
       !output.verification.signature_verified ||
       output.verification.manifest.release_counter != output.counter ||
       !installed_files_match(output.directory.get(), package, output.verification)) {
        error = "SESSION_RELEASE_VERIFICATION_FAILED";
        return false;
    }
    const auto profile_file = std::find_if(
        output.verification.manifest.files.begin(), output.verification.manifest.files.end(),
        [](const auto& file) { return file.path == "profile.env"; });
    if(profile_file == output.verification.manifest.files.end() || profile_file->size > 4096) {
        error = "SESSION_PROFILE_NOT_SIGNED";
        return false;
    }
    const auto* profile_bytes = package.data() + output.verification.payload_start +
                                profile_file->payload_offset;
    const auto parsed = lvgl_platform::parse_session_profile(std::string_view(
        reinterpret_cast<const char*>(profile_bytes), static_cast<std::size_t>(profile_file->size)));
    if(!parsed.ok() ||
       !lvgl_platform::package_supports_session_profile(output.verification.manifest,
                                                        parsed.profile)) {
        error = parsed.ok() ? "SESSION_PROFILE_PACKAGE_MISMATCH" : parsed.detail;
        return false;
    }
    struct utsname identity {};
    if(::uname(&identity) != 0 || parsed.profile.machine != identity.machine) {
        error = "SESSION_MACHINE_MISMATCH";
        return false;
    }
    output.profile = parsed.profile;
    return true;
}

bool reserved_application_id(std::string_view app_id) noexcept
{
    return app_id == "top.lvgl.platform" || app_id == "top.lvgl.desktop" ||
           app_id == "top.lvgl.installer";
}

bool verify_application_release(
    std::string_view app_id, std::uint64_t release_counter,
    const lvgl_platform::Sha512Digest& expected_digest, std::string_view signing_key_id,
    const lvgl_platform::CertifiedSessionProfile& profile,
    lvgl_platform::CryptoProvider& crypto, VerifiedApplication& output) noexcept
{
    if(!lvgl_platform::valid_session_app_id(app_id) || reserved_application_id(app_id) ||
       release_counter == 0 || signing_key_id.size() != 32) {
        return false;
    }
    const auto trust = lvgl_platform::OfficialTrustStore::compiled();
    if(!trust.configured()) return false;
    const std::string release_path = std::string(kApplicationStore) + "/apps/" +
                                     std::string(app_id) + "/releases/" +
                                     std::to_string(release_counter);
    output.release = open_absolute_directory(release_path);
    if(!trusted_directory(output.release.get())) return false;
    auto package_file = open_release_file(output.release.get(), ".package.lvapp");
    if(!read_all(package_file.get(), output.package, kMaximumPackageSize)) return false;
    output.verification = trust.verify(output.package.data(), output.package.size(), crypto);
    lvgl_platform::Sha512Digest digest {};
    if(!output.verification.ok() || output.verification.development ||
       !output.verification.signature_verified ||
       !crypto.sha512(output.package.data(), output.package.size(), digest) ||
       digest != expected_digest || output.verification.manifest.app_id != app_id ||
       output.verification.manifest.release_counter != release_counter ||
       output.verification.manifest.signing_key_id != signing_key_id ||
       !installed_files_match(output.release.get(), output.package, output.verification)) {
        return false;
    }
    const lvgl_platform::InstallPolicyContext policy {
        lvgl_platform::kPlatformVersion, lvgl_platform::kSdkAbi, profile.profile_id,
        profile.machine, {"storage.private"}};
    return lvgl_platform::evaluate_install_policy(
               output.verification, digest, policy).allowed();
}

bool verify_installed_application(
    std::string_view app_id, const lvgl_platform::CertifiedSessionProfile& profile,
    VerifiedApplication& output) noexcept
{
    if(!lvgl_platform::valid_session_app_id(app_id) || reserved_application_id(app_id)) return false;
    const auto crypto = lvgl_platform::CryptoProvider::load_default();
    if(crypto == nullptr) return false;
    const auto state = lvgl_platform::load_release_state(
        kApplicationPolicyStore, std::string(app_id), *crypto);
    if((state.status != lvgl_platform::StateStoreStatus::loaded &&
        state.status != lvgl_platform::StateStoreStatus::loaded_degraded) ||
       state.selected.state.current_release == 0) {
        return false;
    }
    const auto& active = state.selected.state;
    return verify_application_release(
        app_id, active.current_release, active.current_digest, active.signing_key_id,
        profile, *crypto, output);
}

bool rollback_application_after_failure(
    std::string_view app_id, const lvgl_platform::CertifiedSessionProfile& profile,
    std::string& detail) noexcept
{
    if(!lvgl_platform::valid_session_app_id(app_id) || reserved_application_id(app_id)) return false;
    const auto crypto = lvgl_platform::CryptoProvider::load_default();
    if(crypto == nullptr) return false;
    const auto stored = lvgl_platform::load_release_state(
        kApplicationPolicyStore, std::string(app_id), *crypto);
    if((stored.status != lvgl_platform::StateStoreStatus::loaded &&
        stored.status != lvgl_platform::StateStoreStatus::loaded_degraded) ||
       stored.selected.state.previous_release == 0) {
        return false;
    }
    const auto& active = stored.selected.state;
    VerifiedApplication previous;
    if(!verify_application_release(
           app_id, active.previous_release, active.previous_digest, active.signing_key_id,
           profile, *crypto, previous)) {
        return false;
    }
    const auto rolled_back = lvgl_platform::rollback_failed_release(active);
    if(!rolled_back.has_value()) return false;
    const auto persisted = lvgl_platform::persist_release_state(
        kApplicationPolicyStore, *rolled_back, *crypto);
    if(!persisted.ok()) return false;
    detail = persisted.status == lvgl_platform::StateStoreStatus::written_degraded
                 ? "应用失败版本已隔离并回滚；策略冗余需要修复"
                 : "应用失败版本已隔离并安全回滚到上一版本";
    return true;
}

std::vector<std::string> installed_application_ids() noexcept
{
    std::vector<std::string> ids;
    auto applications = open_absolute_directory(std::string(kApplicationStore) + "/apps");
    if(!applications.valid()) return ids;
    const int duplicate = ::dup(applications.get());
    if(duplicate < 0) return ids;
    std::unique_ptr<DIR, DirectoryCloser> directory(::fdopendir(duplicate));
    if(!directory) {
        ::close(duplicate);
        return ids;
    }
    errno = 0;
    while(const auto* entry = ::readdir(directory.get())) {
        const std::string_view name(entry->d_name);
        if(name == "." || name == ".." || !lvgl_platform::valid_session_app_id(name) ||
           reserved_application_id(name)) {
            continue;
        }
        struct stat details {};
        if(::fstatat(applications.get(), entry->d_name, &details, AT_SYMLINK_NOFOLLOW) == 0 &&
           S_ISDIR(details.st_mode) && details.st_uid == 0 && (details.st_mode & 0022) == 0) {
            ids.emplace_back(name);
        }
        if(ids.size() > lvgl_platform::kMaximumRegistryApplications) return {};
    }
    if(errno != 0) return {};
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

bool application_uid_is_unique(std::string_view app_id) noexcept
{
    const uid_t selected = application_uid(app_id);
    constexpr std::string_view built_ins[] {
        "top.lvgl.platform", "top.lvgl.desktop", "top.lvgl.installer",
        "top.lvgl.game2048",
    };
    for(const auto candidate : built_ins) {
        if(candidate != app_id && application_uid(candidate) == selected) return false;
    }
    for(const auto& candidate : installed_application_ids()) {
        if(candidate != app_id && application_uid(candidate) == selected) return false;
    }
    return true;
}

bool write_all(int descriptor, const void* data, std::size_t size) noexcept
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t written = 0;
    while(written < size) {
        const auto count = ::write(descriptor, bytes + written, size - written);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return false;
        written += static_cast<std::size_t>(count);
    }
    return true;
}

bool atomic_status(int run_directory, const lvgl_platform::SessionStatusDocument& status) noexcept
{
    const auto contents = lvgl_platform::encode_session_status(status);
    if(contents.empty()) return false;
    const std::string temporary = ".session.status-" + std::to_string(::getpid());
    FileDescriptor output(::openat(
        run_directory, temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0400));
    if(!output.valid()) {
        if(errno != EEXIST || ::unlinkat(run_directory, temporary.c_str(), 0) != 0) return false;
        output.reset(::openat(
            run_directory, temporary.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0400));
    }
    if(!output.valid() || !write_all(output.get(), contents.data(), contents.size()) ||
       ::fsync(output.get()) != 0 ||
       ::renameat(run_directory, temporary.c_str(), run_directory, "session.status") != 0 ||
       ::fsync(run_directory) != 0) {
        ::unlinkat(run_directory, temporary.c_str(), 0);
        return false;
    }
    return true;
}

FileDescriptor atomic_registry(
    int run_directory, const std::vector<std::uint8_t>& contents) noexcept
{
    if(contents.empty() || contents.size() > lvgl_platform::kMaximumRegistrySize) return {};
    const std::string temporary = ".application.registry-" + std::to_string(::getpid());
    FileDescriptor output(::openat(
        run_directory, temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0400));
    if(!output.valid()) {
        if(errno != EEXIST || ::unlinkat(run_directory, temporary.c_str(), 0) != 0) return {};
        output.reset(::openat(
            run_directory, temporary.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0400));
    }
    if(!output.valid() || !write_all(output.get(), contents.data(), contents.size()) ||
       ::fsync(output.get()) != 0 ||
       ::renameat(run_directory, temporary.c_str(), run_directory, "application.registry") != 0 ||
       ::fsync(run_directory) != 0) {
        ::unlinkat(run_directory, temporary.c_str(), 0);
        return {};
    }
    output.reset();
    FileDescriptor registry(::openat(
        run_directory, "application.registry", O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    return trusted_regular(registry.get(), 0400, contents.size()) ? std::move(registry)
                                                                 : FileDescriptor {};
}

bool prepare_run_root(FileDescriptor& directory, FileDescriptor& lock) noexcept
{
    auto run = open_absolute_directory("/run");
    if(!run.valid()) return false;
    if(::mkdirat(run.get(), "lvgl-platform", 0700) != 0 && errno != EEXIST) return false;
    directory.reset(::openat(
        run.get(), "lvgl-platform", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if(!trusted_directory(directory.get())) return false;
    lock.reset(::openat(
        directory.get(), "session.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600));
    struct stat details {};
    return lock.valid() && ::fstat(lock.get(), &details) == 0 && S_ISREG(details.st_mode) &&
           details.st_uid == 0 && details.st_nlink == 1 && (details.st_mode & 0077) == 0 &&
           ::flock(lock.get(), LOCK_EX | LOCK_NB) == 0;
}

bool remove_old_socket(int run_directory) noexcept
{
    struct stat details {};
    if(::fstatat(run_directory, "touch.sock", &details, AT_SYMLINK_NOFOLLOW) != 0) {
        return errno == ENOENT;
    }
    return S_ISSOCK(details.st_mode) && details.st_uid == 0 &&
           (details.st_mode & 0022) == 0 && ::unlinkat(run_directory, "touch.sock", 0) == 0;
}

bool bind_touch_socket(int run_directory, FileDescriptor& socket_descriptor) noexcept
{
    if(!remove_old_socket(run_directory)) return false;
    socket_descriptor.reset(::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if(!socket_descriptor.valid()) return false;
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, kTouchPath, sizeof(address.sun_path) - 1);
    return ::bind(socket_descriptor.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
               0 &&
           ::chmod(kTouchPath, 0600) == 0;
}

std::uint64_t random_nonce() noexcept
{
    std::uint64_t nonce = 0;
    std::size_t received = 0;
    while(received < sizeof(nonce)) {
        const auto count = ::getrandom(
            reinterpret_cast<std::uint8_t*>(&nonce) + received, sizeof(nonce) - received, 0);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return 0;
        received += static_cast<std::size_t>(count);
    }
    return nonce;
}

std::uint64_t monotonic_microseconds() noexcept
{
    timespec now {};
    if(::clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) return 0;
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000ULL +
           static_cast<std::uint64_t>(now.tv_nsec / 1000);
}

bool inherit_descriptor(int descriptor) noexcept
{
    const int flags = ::fcntl(descriptor, F_GETFD);
    return flags >= 0 && ::fcntl(descriptor, F_SETFD, flags & ~FD_CLOEXEC) == 0;
}

bool set_limit(int resource, rlim_t value) noexcept
{
    const rlimit limit {value, value};
    return ::setrlimit(resource, &limit) == 0;
}

bool apply_child_policy(const ProgramPolicy& policy) noexcept
{
    if(policy.uid == 0 || policy.gid == 0 || policy.limits.memory_mib < 8 ||
       policy.limits.cpu_seconds == 0 || policy.limits.maximum_files == 0 ||
       policy.limits.data_mib == 0) {
        return false;
    }
    const rlim_t memory = static_cast<rlim_t>(policy.limits.memory_mib) * 1024U * 1024U;
    const rlim_t descriptors = std::max<rlim_t>(16, policy.limits.maximum_files);
    const rlim_t stack = std::min<rlim_t>(8U * 1024U * 1024U, memory / 4U);
    constexpr rlim_t kMaximumInheritedLogBytes = 1024U * 1024U;
    if(!set_limit(RLIMIT_AS, memory) || !set_limit(RLIMIT_CPU, policy.limits.cpu_seconds) ||
       !set_limit(RLIMIT_CORE, 0) || !set_limit(RLIMIT_FSIZE, kMaximumInheritedLogBytes) ||
       !set_limit(RLIMIT_NOFILE, descriptors) || !set_limit(RLIMIT_NPROC, 1) ||
       !set_limit(RLIMIT_MEMLOCK, 0) || !set_limit(RLIMIT_STACK, stack) ||
       ::prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0 || ::getppid() == 1 ||
       ::setgroups(0, nullptr) != 0 || ::setresgid(policy.gid, policy.gid, policy.gid) != 0 ||
       ::setresuid(policy.uid, policy.uid, policy.uid) != 0 ||
       ::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0 || ::chdir("/") != 0) {
        return false;
    }
    return ::geteuid() == policy.uid && ::getegid() == policy.gid;
}

FileDescriptor open_drm_device(
    const lvgl_platform::CertifiedSessionProfile& profile) noexcept
{
    FileDescriptor descriptor(::open(
        profile.drm_device.c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC));
    struct stat details {};
    if(!descriptor.valid() || ::fstat(descriptor.get(), &details) != 0 ||
       !S_ISCHR(details.st_mode) || details.st_uid != 0 || details.st_nlink != 1) {
        return {};
    }
    return descriptor;
}

pid_t start_program(
    int executable, int control, int touch, int registry, int storage, int installer, int drm,
    std::string_view program_id, const ProgramPolicy& policy,
    const lvgl_platform::CertifiedSessionProfile& profile, std::string_view previous_error) noexcept
{
    const pid_t child = ::fork();
    if(child != 0) return child;
    if(::setpgid(0, 0) != 0 || !inherit_descriptor(control) || !inherit_descriptor(touch) ||
       (registry >= 0 && !inherit_descriptor(registry)) ||
       (storage >= 0 && !inherit_descriptor(storage)) ||
       (installer >= 0 && !inherit_descriptor(installer)) || !inherit_descriptor(drm)) {
        _exit(126);
    }
    const long limit_value = ::sysconf(_SC_OPEN_MAX);
    const int limit = static_cast<int>(
        std::min<long>(limit_value > 0 ? limit_value : 1024, 65536));
    for(int fd = STDERR_FILENO + 1; fd < limit; ++fd) {
        if(fd != executable && fd != control && fd != touch && fd != registry && fd != storage &&
           fd != installer && fd != drm) {
            ::close(fd);
        }
    }
    if(!apply_child_policy(policy)) _exit(125);
    std::vector<std::string> environment {
        "PATH=/usr/sbin:/usr/bin:/sbin:/bin",
        "LVGL_SESSION_CONTROL_FD=" + std::to_string(control),
        "LVGL_TOUCH_FD=" + std::to_string(touch),
        "LVGL_DRM_FD=" + std::to_string(drm),
        "LVGL_SANDBOX_REQUIRED=1",
        "LVGL_APP_UID=" + std::to_string(policy.uid),
        "LVGL_LOGICAL_WIDTH=" + std::to_string(profile.logical_width),
        "LVGL_LOGICAL_HEIGHT=" + std::to_string(profile.logical_height),
        "LVGL_DRM_DEVICE=" + profile.drm_device,
        "LVGL_DRM_CONNECTOR_ID=" + std::to_string(profile.connector_id),
        "LVGL_DRM_CRTC_ID=" + std::to_string(profile.crtc_id),
        "LVGL_DRM_OVERLAY_PLANE_ID=" + std::to_string(profile.overlay_plane_id),
        "LVGL_DRM_OVERLAY_ZPOS=" + std::to_string(profile.overlay_zpos),
        "LVGL_DISPLAY_X=" + std::to_string(profile.display_x),
        "LVGL_DISPLAY_Y=" + std::to_string(profile.display_y),
        "LVGL_DISPLAY_WIDTH=" + std::to_string(profile.display_width),
        "LVGL_DISPLAY_HEIGHT=" + std::to_string(profile.display_height),
        "LVGL_DISPLAY_ROTATION=" + std::to_string(profile.display_rotation),
        "LVGL_PIXEL_FORMAT=" + profile.pixel_format,
        "LVGL_PROFILE_ID=" + profile.profile_id,
        "LVGL_MACHINE=" + profile.machine,
        "LVGL_FOREGROUND_APP_ID=" + std::string(program_id),
    };
    if(registry >= 0) {
        environment.push_back("LVGL_APP_REGISTRY_FD=" + std::to_string(registry));
    }
    if(storage >= 0) {
        environment.push_back("LVGL_APP_STORAGE_FD=" + std::to_string(storage));
    }
    if(installer >= 0) {
        environment.push_back("LVGL_INSTALLER_FD=" + std::to_string(installer));
    }
    if(!previous_error.empty()) {
        environment.push_back("LVGL_APP_ERROR=" + std::string(previous_error));
    }
    std::vector<char*> environment_pointers;
    for(auto& item : environment) environment_pointers.push_back(item.data());
    environment_pointers.push_back(nullptr);
    std::string name(program_id == "top.lvgl.desktop" ? "lvgl-desktop" : "lvgl-application");
    char* const arguments[] {name.data(), nullptr};
    ::fexecve(executable, arguments, environment_pointers.data());
    _exit(127);
}

int child_result(pid_t child, int options, bool& exited) noexcept
{
    int status = 0;
    const auto waited = ::waitpid(child, &status, options);
    if(waited == 0 || (waited < 0 && errno == EINTR)) return 0;
    exited = true;
    if(waited < 0) return 255;
    if(WIFEXITED(status)) return WEXITSTATUS(status);
    if(WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 255;
}

const char* built_in_program_path(std::string_view app_id) noexcept
{
    if(app_id == "top.lvgl.desktop") return "bin/lvgl-desktop";
    if(app_id == "top.lvgl.game2048") return "bin/lvgl-2048";
    if(app_id == "top.lvgl.installer") return "bin/lvgl-installer";
    return nullptr;
}

FileDescriptor open_verified_program(
    const VerifiedRelease& release, const lvgl_platform::CertifiedSessionProfile& profile,
    std::string_view app_id, ProgramPolicy* policy = nullptr)
{
    if(policy != nullptr) {
        if(!application_uid_is_unique(app_id)) return {};
        const auto uid = application_uid(app_id);
        *policy = {{}, false, uid, static_cast<gid_t>(uid)};
    }
    if(!reserved_application_id(app_id)) {
        VerifiedApplication application;
        if(verify_installed_application(app_id, profile, application)) {
            const auto& manifest = application.verification.manifest;
            const auto entry = std::find_if(
                manifest.files.begin(), manifest.files.end(),
                [&manifest](const auto& file) { return file.path == manifest.entry; });
            auto executable = open_release_file(application.release.get(), manifest.entry);
            if(entry != manifest.files.end() && entry->role == "executable" &&
               entry->mode == 0755 && trusted_regular(executable.get(), 0755, entry->size)) {
                if(policy != nullptr) {
                    policy->limits = manifest.limits;
                    policy->private_storage = std::find(
                        manifest.capabilities.begin(), manifest.capabilities.end(),
                        "storage.private") != manifest.capabilities.end();
                }
                return executable;
            }
            if(built_in_program_path(app_id) == nullptr) return {};
        }
    }
    const char* path = built_in_program_path(app_id);
    if(path != nullptr) {
        const auto manifest_file = std::find_if(
            release.verification.manifest.files.begin(), release.verification.manifest.files.end(),
            [path](const auto& file) { return file.path == path; });
        auto executable = open_release_file(release.directory.get(), path);
        if(manifest_file == release.verification.manifest.files.end() ||
           manifest_file->role != "executable" || manifest_file->mode != 0755 ||
           !trusted_regular(executable.get(), 0755, manifest_file->size)) {
            return {};
        }
        if(policy != nullptr) {
            policy->limits = release.verification.manifest.limits;
            policy->private_storage = app_id == "top.lvgl.game2048";
        }
        return executable;
    }
    return {};
}

FileDescriptor build_desktop_registry(const VerifiedRelease& release, int run_directory)
{
    std::vector<lvgl_platform::RegisteredApplication> applications;
    for(const auto& app_id : installed_application_ids()) {
        VerifiedApplication application;
        if(!verify_installed_application(app_id, release.profile, application)) continue;
        const auto& manifest = application.verification.manifest;
        applications.push_back({manifest.app_id, manifest.name, manifest.version,
                                manifest.release_counter, manifest.security_epoch,
                                static_cast<std::uint32_t>(manifest.capabilities.size())});
    }
    const bool has_game = std::any_of(
        applications.begin(), applications.end(), [](const auto& application) {
            return application.app_id == "top.lvgl.game2048";
        });
    if(!has_game &&
       open_verified_program(release, release.profile, "top.lvgl.game2048").valid()) {
        applications.push_back({"top.lvgl.game2048", "2048", "1.0.0", release.counter,
                                release.verification.manifest.security_epoch, 0});
    }
    if(open_verified_program(release, release.profile, "top.lvgl.installer").valid()) {
        applications.push_back({"top.lvgl.installer", "应用安装器", "1.0.0", release.counter,
                                release.verification.manifest.security_epoch, 0});
    }
    std::sort(applications.begin(), applications.end(), [](const auto& left, const auto& right) {
        return left.app_id < right.app_id;
    });
    const auto encoded = lvgl_platform::encode_application_registry(applications);
    return encoded.ok() ? atomic_registry(run_directory, encoded.bytes) : FileDescriptor {};
}

lvgl_platform::InstallerProtocolStatus installer_status(
    lvgl_platform::InboxStatus status) noexcept
{
    using Inbox = lvgl_platform::InboxStatus;
    using Protocol = lvgl_platform::InstallerProtocolStatus;
    switch(status) {
        case Inbox::ready: return Protocol::ready;
        case Inbox::empty: return Protocol::empty;
        case Inbox::invalid_token: return Protocol::invalid_request;
        case Inbox::candidate_not_found: return Protocol::not_found;
        case Inbox::candidate_rejected: return Protocol::rejected;
        case Inbox::root_untrusted: return Protocol::unavailable;
        case Inbox::io_error: return Protocol::io_error;
        case Inbox::unsupported_platform: return Protocol::unavailable;
    }
    return Protocol::io_error;
}

lvgl_platform::InstallPolicyContext installer_policy(
    const lvgl_platform::CertifiedSessionProfile& profile)
{
    return {lvgl_platform::kPlatformVersion, lvgl_platform::kSdkAbi,
            profile.profile_id, profile.machine, {"storage.private"}};
}

bool handle_installer_request(
    int socket, const lvgl_platform::CertifiedSessionProfile& profile) noexcept
{
    std::array<std::uint8_t, lvgl_platform::kInstallerRequestSize> request_bytes {};
    const auto received = ::recv(
        socket, request_bytes.data(), request_bytes.size(), MSG_DONTWAIT | MSG_TRUNC);
    lvgl_platform::InstallerRequest request;
    if(received != static_cast<ssize_t>(request_bytes.size()) ||
       !lvgl_platform::decode_installer_request(
           request_bytes.data(), request_bytes.size(), request)) {
        return false;
    }
    const auto crypto = lvgl_platform::CryptoProvider::load_default();
    if(crypto == nullptr) return false;
    lvgl_platform::InstallerResponse response;
    response.command = request.command;
    response.request_id = request.request_id;
    response.status = lvgl_platform::InstallerProtocolStatus::io_error;
    response.detail = "INSTALLER_BROKER_FAILED";
    const auto policy = installer_policy(profile);
    const auto prepared = lvgl_platform::prepare_application_storage();
    if(prepared != lvgl_platform::InboxStatus::ready) {
        response.status = installer_status(prepared);
        response.detail = "INSTALLER_STORAGE_UNAVAILABLE";
    } else if(request.command == lvgl_platform::InstallerCommand::install) {
        const auto result = lvgl_platform::install_official_inbox_candidate(
            request.token, policy, *crypto);
        response.status = installer_status(result.status);
        response.detail = result.detail;
    } else {
        const auto scan = lvgl_platform::scan_official_inbox(policy, *crypto);
        response.status = installer_status(scan.status);
        response.detail = scan.detail;
        if(request.command == lvgl_platform::InstallerCommand::scan) {
            if(scan.status == lvgl_platform::InboxStatus::ready) {
                response.count = static_cast<std::uint32_t>(scan.candidates.size());
            }
        } else if(scan.status == lvgl_platform::InboxStatus::ready &&
                  request.index < scan.candidates.size()) {
            response.status = lvgl_platform::InstallerProtocolStatus::ready;
            response.detail = scan.candidates[request.index].detail;
            response.candidate = scan.candidates[request.index];
        } else {
            response.status = lvgl_platform::InstallerProtocolStatus::not_found;
            response.detail = "INBOX_CANDIDATE_NOT_FOUND";
        }
    }
    const auto encoded = lvgl_platform::encode_installer_response(response);
    return encoded[0] != 0 &&
           ::send(socket, encoded.data(), encoded.size(), MSG_NOSIGNAL) ==
               static_cast<ssize_t>(encoded.size());
}

enum class ForegroundAction : std::uint8_t { failure, exit_session, launch, home };

struct ForegroundOutcome {
    ForegroundAction action {ForegroundAction::failure};
    std::string app_id;
    int result {0};
};

ForegroundOutcome run_foreground(
    int executable, std::string_view program_id, std::string_view previous_error,
    const lvgl_platform::CertifiedSessionProfile& profile, int run_directory,
    int external_touch_socket, int registry, int storage_directory,
    const ProgramPolicy& policy, lvgl_platform::TouchRouter& router,
    lvgl_platform::SessionStatusDocument& status)
{
    ForegroundOutcome outcome;
    int control_pair[2] {-1, -1};
    int touch_pair[2] {-1, -1};
    int storage_pair[2] {-1, -1};
    int installer_pair[2] {-1, -1};
    const bool installer_program = program_id == "top.lvgl.installer";
    if(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control_pair) != 0 ||
       ::socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, touch_pair) != 0 ||
       (storage_directory >= 0 &&
        ::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, storage_pair) != 0) ||
       (installer_program &&
        ::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, installer_pair) != 0)) {
        if(control_pair[0] >= 0) ::close(control_pair[0]);
        if(control_pair[1] >= 0) ::close(control_pair[1]);
        if(touch_pair[0] >= 0) ::close(touch_pair[0]);
        if(touch_pair[1] >= 0) ::close(touch_pair[1]);
        if(storage_pair[0] >= 0) ::close(storage_pair[0]);
        if(storage_pair[1] >= 0) ::close(storage_pair[1]);
        if(installer_pair[0] >= 0) ::close(installer_pair[0]);
        if(installer_pair[1] >= 0) ::close(installer_pair[1]);
        outcome.result = 76;
        return outcome;
    }
    FileDescriptor parent_control(control_pair[0]);
    FileDescriptor child_control(control_pair[1]);
    FileDescriptor parent_touch(touch_pair[0]);
    FileDescriptor child_touch(touch_pair[1]);
    FileDescriptor parent_storage(storage_pair[0]);
    FileDescriptor child_storage(storage_pair[1]);
    FileDescriptor parent_installer(installer_pair[0]);
    FileDescriptor child_installer(installer_pair[1]);
    auto drm = open_drm_device(profile);
    if(!drm.valid()) {
        outcome.result = 87;
        return outcome;
    }
    if(parent_storage.valid()) {
        const int buffer_size = static_cast<int>(lvgl_platform::kMaximumStoragePacketSize * 2U);
            if(::setsockopt(
                   parent_storage.get(), SOL_SOCKET, SO_RCVBUF, &buffer_size,
                   sizeof(buffer_size)) != 0 ||
               ::setsockopt(
                   parent_storage.get(), SOL_SOCKET, SO_SNDBUF, &buffer_size,
                   sizeof(buffer_size)) != 0 ||
               ::setsockopt(
                   child_storage.get(), SOL_SOCKET, SO_RCVBUF, &buffer_size,
                   sizeof(buffer_size)) != 0 ||
               ::setsockopt(
                   child_storage.get(), SOL_SOCKET, SO_SNDBUF, &buffer_size,
               sizeof(buffer_size)) != 0) {
            outcome.result = 88;
            return outcome;
        }
    }
    if(parent_installer.valid()) {
        const int buffer_size = static_cast<int>(lvgl_platform::kInstallerResponseSize * 4U);
        const timeval timeout {30, 0};
        if(::setsockopt(
               parent_installer.get(), SOL_SOCKET, SO_RCVBUF, &buffer_size,
               sizeof(buffer_size)) != 0 ||
           ::setsockopt(
               parent_installer.get(), SOL_SOCKET, SO_SNDBUF, &buffer_size,
               sizeof(buffer_size)) != 0 ||
           ::setsockopt(
               child_installer.get(), SOL_SOCKET, SO_RCVBUF, &buffer_size,
               sizeof(buffer_size)) != 0 ||
           ::setsockopt(
               child_installer.get(), SOL_SOCKET, SO_SNDBUF, &buffer_size,
               sizeof(buffer_size)) != 0 ||
           ::setsockopt(
               child_installer.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout,
               sizeof(timeout)) != 0 ||
           ::setsockopt(
               child_installer.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout,
               sizeof(timeout)) != 0) {
            outcome.result = 90;
            return outcome;
        }
    }
    const pid_t child = start_program(
        executable, child_control.get(), child_touch.get(), registry, child_storage.get(),
        child_installer.get(), drm.get(), program_id, policy, profile, previous_error);
    if(child <= 1) {
        outcome.result = 77;
        return outcome;
    }
    ::setpgid(child, child);
    child_control.reset();
    child_touch.reset();
    child_storage.reset();
    child_installer.reset();

    const auto ready_deadline = monotonic_microseconds() +
                                static_cast<std::uint64_t>(kReadyTimeoutMilliseconds) * 1000ULL;
    const bool desktop = program_id == "top.lvgl.desktop";
    bool ready = false;
    bool requested = false;
    bool exited = false;
    std::array<std::uint8_t, lvgl_platform::kTouchFrameWireSize> touch_frame {};
    std::array<std::uint8_t, lvgl_platform::kSessionControlSize> control_frame {};

    while(!exited && g_stop == 0 && !requested) {
        pollfd descriptors[] {
            {parent_control.get(), POLLIN | POLLHUP, 0},
            {external_touch_socket, static_cast<short>(ready ? POLLIN : 0), 0},
            {parent_storage.get(), static_cast<short>(parent_storage.valid() ? POLLIN : 0), 0},
            {parent_installer.get(), static_cast<short>(parent_installer.valid() ? POLLIN : 0), 0},
        };
        const int polled = ::poll(descriptors, 4, 100);
        if(polled < 0 && errno != EINTR) {
            outcome.result = 78;
            break;
        }
        if(polled > 0 && (descriptors[0].revents & POLLIN) != 0) {
            const auto received = ::recv(
                parent_control.get(), control_frame.data(), control_frame.size(),
                MSG_DONTWAIT | MSG_TRUNC);
            lvgl_platform::SessionControlMessage message;
            if(received != static_cast<ssize_t>(control_frame.size()) ||
               !lvgl_platform::decode_session_control(
                   control_frame.data(), control_frame.size(), message)) {
                outcome.result = 79;
                break;
            }
            if(message.command == lvgl_platform::SessionControlCommand::ready && !ready) {
                ready = true;
                status.state = lvgl_platform::SessionState::ready;
                status.hole_ready = true;
                status.input_ready = true;
                if(!atomic_status(run_directory, status)) {
                    outcome.result = 80;
                    break;
                }
            } else if(message.command == lvgl_platform::SessionControlCommand::exit_session && ready) {
                outcome.action = ForegroundAction::exit_session;
                requested = true;
            } else if(message.command == lvgl_platform::SessionControlCommand::launch_application &&
                      ready && desktop && message.app_id != "top.lvgl.desktop" &&
                      (built_in_program_path(message.app_id) != nullptr ||
                       !reserved_application_id(message.app_id))) {
                outcome.action = ForegroundAction::launch;
                outcome.app_id = std::move(message.app_id);
                requested = true;
            } else if(message.command == lvgl_platform::SessionControlCommand::home && ready &&
                      !desktop) {
                outcome.action = ForegroundAction::home;
                requested = true;
            } else {
                outcome.result = 81;
                break;
            }
        }
        if(polled > 0 && ready && (descriptors[1].revents & POLLIN) != 0) {
            const auto received = ::recv(
                external_touch_socket, touch_frame.data(), touch_frame.size(),
                MSG_DONTWAIT | MSG_TRUNC);
            const auto routed = router.route(
                touch_frame.data(), received > 0 ? static_cast<std::size_t>(received) : 0,
                monotonic_microseconds());
            if(routed.ok()) {
                const auto encoded = lvgl_platform::encode_touch_frame(routed.frame);
                if(::send(parent_touch.get(), encoded.data(), encoded.size(), MSG_NOSIGNAL) !=
                   static_cast<ssize_t>(encoded.size())) {
                    outcome.result = 82;
                    break;
                }
            }
        }
        if(polled > 0 && parent_storage.valid() &&
           (descriptors[2].revents & POLLIN) != 0 &&
           !handle_storage_request(
               parent_storage.get(), storage_directory, policy.limits)) {
            outcome.result = 89;
            break;
        }
        if(polled > 0 && parent_installer.valid() &&
           (descriptors[3].revents & POLLIN) != 0 &&
           !handle_installer_request(parent_installer.get(), profile)) {
            outcome.result = 91;
            break;
        }
        if(!ready && monotonic_microseconds() >= ready_deadline) {
            outcome.result = 83;
            break;
        }
        const int child_exit = child_result(child, WNOHANG, exited);
        if(exited) outcome.result = child_exit == 0 ? 84 : child_exit;
    }

    if(!exited) {
        ::kill(child, SIGTERM);
        for(int attempt = 0; attempt < 20 && !exited; ++attempt) {
            ::usleep(50000);
            const int child_exit = child_result(child, WNOHANG, exited);
            if(exited && outcome.result == 0 && !requested) outcome.result = child_exit;
        }
        if(!exited) {
            ::kill(child, SIGKILL);
            const int child_exit = child_result(child, 0, exited);
            if(outcome.result == 0 && !requested) outcome.result = child_exit;
        }
    }
    if(g_stop != 0 && outcome.result == 0) outcome.result = 143;
    return outcome;
}

}  // namespace

int main(int argc, char** argv)
{
    if(argc != 2 || std::string_view(argv[1]) != "--falcon-hole" || ::geteuid() != 0) return 64;
    ::umask(0077);
    if(::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0 ||
       ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return 70;
    }
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGHUP, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    VerifiedRelease release;
    std::string verification_error;
    if(!verify_active_release(release, verification_error)) return 71;

    FileDescriptor run_directory;
    FileDescriptor lock;
    FileDescriptor touch_socket;
    if(!prepare_run_root(run_directory, lock) ||
       !bind_touch_socket(run_directory.get(), touch_socket)) {
        return 72;
    }
    const auto cleanup_socket = [&]() { ::unlinkat(run_directory.get(), "touch.sock", 0); };
    const auto nonce = random_nonce();
    if(nonce == 0) {
        cleanup_socket();
        return 73;
    }

    lvgl_platform::SessionStatusDocument status {
        static_cast<std::int32_t>(::getpid()), nonce, lvgl_platform::SessionState::starting,
        0, false, false, release.profile.logical_width, release.profile.logical_height};
    if(!atomic_status(run_directory.get(), status)) {
        cleanup_socket();
        return 74;
    }

    lvgl_platform::TouchRouter router(
        nonce, release.profile.logical_width, release.profile.logical_height);
    std::string foreground = "top.lvgl.desktop";
    std::string previous_error;
    unsigned int desktop_failures = 0;
    int result = 0;
    bool exit_requested = false;
    while(g_stop == 0 && !exit_requested) {
        status.state = lvgl_platform::SessionState::starting;
        status.result = 0;
        status.hole_ready = false;
        status.input_ready = false;
        if(!atomic_status(run_directory.get(), status)) {
            result = 74;
            break;
        }
        ProgramPolicy policy;
        auto executable = open_verified_program(
            release, release.profile, foreground, &policy);
        if(!executable.valid()) {
            if(foreground != "top.lvgl.desktop") {
                if(!rollback_application_after_failure(
                       foreground, release.profile, previous_error)) {
                    previous_error = "应用入口未包含在已验证的平台发行版中";
                }
                foreground = "top.lvgl.desktop";
                continue;
            }
            result = 75;
            break;
        }
        FileDescriptor registry;
        if(foreground == "top.lvgl.desktop") {
            registry = build_desktop_registry(release, run_directory.get());
            if(!registry.valid()) {
                result = 85;
                break;
            }
        }
        FileDescriptor storage;
        if(policy.private_storage) {
            storage = ensure_private_storage(foreground);
            if(!storage.valid()) {
                if(foreground != "top.lvgl.desktop") {
                    previous_error = "应用私有存储不可用";
                    foreground = "top.lvgl.desktop";
                    continue;
                }
                result = 86;
                break;
            }
        }
        const auto outcome = run_foreground(
            executable.get(), foreground, previous_error, release.profile,
            run_directory.get(), touch_socket.get(), registry.get(), storage.get(), policy,
            router, status);
        previous_error.clear();
        if(g_stop != 0) {
            result = outcome.result == 0 ? 143 : outcome.result;
            break;
        }
        if(outcome.action == ForegroundAction::exit_session) {
            exit_requested = true;
            result = 0;
        } else if(outcome.action == ForegroundAction::launch) {
            foreground = outcome.app_id;
            desktop_failures = 0;
        } else if(outcome.action == ForegroundAction::home) {
            foreground = "top.lvgl.desktop";
            desktop_failures = 0;
        } else if(foreground != "top.lvgl.desktop") {
            if(!rollback_application_after_failure(
                   foreground, release.profile, previous_error)) {
                previous_error = "应用异常退出，代码 " + std::to_string(outcome.result);
            }
            foreground = "top.lvgl.desktop";
            desktop_failures = 0;
        } else {
            result = outcome.result == 0 ? 84 : outcome.result;
            if(++desktop_failures >= 3) break;
            previous_error = "桌面已从异常退出中恢复";
        }
    }
    status.state = result == 0 ? lvgl_platform::SessionState::stopped
                               : lvgl_platform::SessionState::error;
    status.result = result;
    status.hole_ready = false;
    status.input_ready = false;
    atomic_status(run_directory.get(), status);
    cleanup_socket();
    return result;
}
