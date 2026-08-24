#include "Manager.hpp"

#include "lvgl_platform/package_installer.h"
#include "lvgl_platform/state_store.h"
#include "lvgl_platform/trust_store.h"
#include "manager_build_config.h"
#include "manager_embedded_payload.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kPlatformRoot = "/userdisk/apps/lvgl-platform";
constexpr const char* kPolicyRoot = "/userdisk/apps/lvgl-platform-policy";
constexpr const char* kPlatformParent = "/userdisk/apps";
constexpr const char* kPlatformDirectory = "lvgl-platform";
constexpr const char* kPlatformAppId = "top.lvgl.platform";
constexpr const char* kSessionStatus = "/run/lvgl-platform/session.status";
constexpr std::size_t kRemovalNodeLimit = 100000;
constexpr unsigned int kRemovalDepthLimit = 32;

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor() { if(value_ >= 0) close(value_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : value_(other.value_) { other.value_ = -1; }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if(this != &other) {
            if(value_ >= 0) close(value_);
            value_ = other.value_;
            other.value_ = -1;
        }
        return *this;
    }
    int get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ >= 0; }
private:
    int value_;
};

class DirectoryStream {
public:
    explicit DirectoryStream(DIR* value) noexcept : value_(value) {}
    ~DirectoryStream() { if(value_ != nullptr) closedir(value_); }
    DirectoryStream(const DirectoryStream&) = delete;
    DirectoryStream& operator=(const DirectoryStream&) = delete;
    DIR* get() const noexcept { return value_; }
private:
    DIR* value_;
};

bool trustedDirectory(int descriptor) noexcept
{
    struct stat details {};
    return descriptor >= 0 && fstat(descriptor, &details) == 0 && S_ISDIR(details.st_mode) &&
           details.st_uid == geteuid() && (details.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

FileDescriptor openTrustedDirectory(const char* path) noexcept
{
    FileDescriptor directory(open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    return trustedDirectory(directory.get()) ? std::move(directory) : FileDescriptor();
}

bool ensureOwnedRoot(int parent, const char* name) noexcept
{
    if(mkdirat(parent, name, 0700) != 0 && errno != EEXIST) return false;
    FileDescriptor root(openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    return trustedDirectory(root.get()) && fsync(parent) == 0;
}

bool readTrustedFile(const char* path, std::size_t maximum, std::vector<std::uint8_t>& output)
{
    FileDescriptor input(open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat details {};
    if(!input.valid() || fstat(input.get(), &details) != 0 || !S_ISREG(details.st_mode) ||
       details.st_uid != 0 || (details.st_mode & (S_IWGRP | S_IWOTH)) != 0 ||
       details.st_size <= 0 || static_cast<std::uint64_t>(details.st_size) > maximum) {
        return false;
    }
    output.assign(static_cast<std::size_t>(details.st_size), 0);
    std::size_t received = 0;
    while(received < output.size()) {
        const auto count = read(input.get(), output.data() + received, output.size() - received);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return false;
        received += static_cast<std::size_t>(count);
    }
    std::uint8_t trailing = 0;
    return read(input.get(), &trailing, 1) == 0;
}

std::string lowerHex(const lvgl_platform::Sha256Digest& digest)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for(const auto value : digest) {
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

bool deviceIdentityMatches(const lvgl_platform::CryptoProvider& crypto)
{
    if(manager_build::kDeviceIdentitySha256Hex.size() != 64) return false;
    struct utsname identity {};
    if(uname(&identity) != 0 || manager_build::kCertifiedMachine != identity.machine) return false;
    std::vector<std::uint8_t> packages;
    std::vector<std::uint8_t> screen;
    if(!readTrustedFile("/etc/miniapp/resources/local_packages.json", 1024U * 1024U, packages) ||
       !readTrustedFile("/etc/miniapp/resources/cfg.json", 64U * 1024U, screen)) {
        return false;
    }
    std::vector<std::uint8_t> evidence;
    const auto append = [&evidence](const void* bytes, std::size_t size) {
        const auto* begin = static_cast<const std::uint8_t*>(bytes);
        evidence.insert(evidence.end(), begin, begin + size);
        evidence.push_back(0);
    };
    append(identity.machine, std::strlen(identity.machine));
    append(packages.data(), packages.size());
    append(screen.data(), screen.size());
    lvgl_platform::Sha256Digest digest {};
    return crypto.sha256(evidence.data(), evidence.size(), digest) &&
           lowerHex(digest) == manager_build::kDeviceIdentitySha256Hex;
}

bool contains(const std::vector<std::string>& values, std::string_view expected)
{
    return std::find_if(values.begin(), values.end(), [expected](const std::string& value) {
        return value == expected;
    }) != values.end();
}

bool releaseDirectoryExists(std::uint64_t release) noexcept
{
    if(release == 0) return false;
    const std::string path = std::string(kPlatformRoot) + "/apps/" + kPlatformAppId +
                             "/releases/" + std::to_string(release);
    auto directory = openTrustedDirectory(path.c_str());
    return directory.valid();
}

bool currentLinkMatches(std::uint64_t release) noexcept
{
    std::array<char, 256> target {};
    const auto count = readlink((std::string(kPlatformRoot) + "/current").c_str(),
                                target.data(), target.size() - 1);
    if(count <= 0 || static_cast<std::size_t>(count) >= target.size()) return false;
    const std::string expected = std::string("apps/") + kPlatformAppId + "/releases/" +
                                 std::to_string(release);
    return std::string_view(target.data(), static_cast<std::size_t>(count)) == expected;
}

bool randomSuffix(std::string& output)
{
    std::array<std::uint8_t, 12> random {};
    std::size_t received = 0;
    while(received < random.size()) {
        const auto count = getrandom(random.data() + received, random.size() - received, 0);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return false;
        received += static_cast<std::size_t>(count);
    }
    constexpr char digits[] = "0123456789abcdef";
    output.clear();
    for(const auto value : random) {
        output.push_back(digits[value >> 4U]);
        output.push_back(digits[value & 0x0fU]);
    }
    return true;
}

bool activateRelease(std::uint64_t release) noexcept
{
    auto root = openTrustedDirectory(kPlatformRoot);
    if(!root.valid() || !releaseDirectoryExists(release)) return false;
    struct stat current {};
    if(fstatat(root.get(), "current", &current, AT_SYMLINK_NOFOLLOW) == 0) {
        if(!S_ISLNK(current.st_mode)) return false;
    } else if(errno != ENOENT) {
        return false;
    }
    std::string suffix;
    if(!randomSuffix(suffix)) return false;
    const std::string temporary = ".current-" + suffix;
    const std::string target = std::string("apps/") + kPlatformAppId + "/releases/" +
                               std::to_string(release);
    if(symlinkat(target.c_str(), root.get(), temporary.c_str()) != 0) return false;
    if(renameat(root.get(), temporary.c_str(), root.get(), "current") != 0) {
        unlinkat(root.get(), temporary.c_str(), 0);
        return false;
    }
    return fsync(root.get()) == 0;
}

bool sessionRemovalAllowed()
{
    struct stat details {};
    if(lstat(kSessionStatus, &details) != 0) return errno == ENOENT;
    std::vector<std::uint8_t> status;
    if(!readTrustedFile(kSessionStatus, 4096, status)) return false;
    const std::string text(status.begin(), status.end());
    return text.find("STATE=stopped\n") != std::string::npos;
}

bool removeContents(int directory, unsigned int depth, std::size_t& nodes)
{
    if(depth > kRemovalDepthLimit) return false;
    DirectoryStream stream(fdopendir(dup(directory)));
    if(stream.get() == nullptr) return false;
    errno = 0;
    while(const auto* entry = readdir(stream.get())) {
        if(std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
        if(++nodes > kRemovalNodeLimit) return false;
        struct stat details {};
        if(fstatat(directory, entry->d_name, &details, AT_SYMLINK_NOFOLLOW) != 0) return false;
        if(S_ISDIR(details.st_mode)) {
            FileDescriptor child(openat(
                directory, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if(!trustedDirectory(child.get()) || !removeContents(child.get(), depth + 1, nodes) ||
               unlinkat(directory, entry->d_name, AT_REMOVEDIR) != 0) {
                return false;
            }
        } else if(unlinkat(directory, entry->d_name, 0) != 0) {
            return false;
        }
        errno = 0;
    }
    return errno == 0 && fsync(directory) == 0;
}

bool cleanupRemovalTombstones(int parent)
{
    constexpr std::string_view prefix = ".lvgl-platform-removed-";
    DirectoryStream stream(fdopendir(dup(parent)));
    if(stream.get() == nullptr) return false;
    std::size_t nodes = 0;
    errno = 0;
    while(const auto* entry = readdir(stream.get())) {
        const std::string_view name(entry->d_name);
        if(name.size() <= prefix.size() || name.substr(0, prefix.size()) != prefix) {
            errno = 0;
            continue;
        }
        if(name.size() != prefix.size() + 24 ||
           !std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefix.size()), name.end(),
                        [](char value) {
                            return (value >= '0' && value <= '9') ||
                                   (value >= 'a' && value <= 'f');
                        })) {
            return false;
        }
        FileDescriptor tombstone(openat(
            parent, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if(!trustedDirectory(tombstone.get()) || !removeContents(tombstone.get(), 0, nodes) ||
           unlinkat(parent, entry->d_name, AT_REMOVEDIR) != 0) {
            return false;
        }
        errno = 0;
    }
    return errno == 0 && fsync(parent) == 0;
}

}  // namespace

Manager::Manager()
    : crypto_(lvgl_platform::CryptoProvider::load_default())
{
}

Manager::~Manager() = default;

bool Manager::verifyBuildAndPayload(
    ManagerSnapshot& snapshot, lvgl_platform::PackageVerification& verification)
{
    snapshot.profileId = std::string(manager_build::kCertifiedProfileId);
    snapshot.payloadAvailable = manager_embedded::kPlatformPackageSize > 0;
    if(!snapshot.payloadAvailable || crypto_ == nullptr ||
       !lvgl_platform::OfficialTrustStore::compiled().configured() ||
       manager_build::kCertifiedProfileId.empty() || manager_build::kCertifiedMachine.empty() ||
       manager_build::kDeviceIdentitySha256Hex.empty()) {
        snapshot.code = "MANAGER_BUILD_UNPROVISIONED";
        snapshot.detail = "production trust root, signed payload, or certified device identity is absent";
        return false;
    }
    verification = lvgl_platform::OfficialTrustStore::compiled().verify(
        manager_embedded::kPlatformPackage, manager_embedded::kPlatformPackageSize, *crypto_);
    if(!verification.ok() || verification.manifest.app_id != kPlatformAppId ||
       verification.manifest.entry != "bin/lvgl-sessiond" ||
       !contains(verification.manifest.supported_profiles, manager_build::kCertifiedProfileId) ||
       !contains(verification.manifest.supported_machines, manager_build::kCertifiedMachine)) {
        snapshot.code = "MANAGER_PAYLOAD_REJECTED";
        snapshot.detail = verification.detail.empty() ? "platform payload identity is invalid"
                                                       : verification.detail;
        return false;
    }
    snapshot.payloadVersion = verification.manifest.version;
    if(!deviceIdentityMatches(*crypto_)) {
        snapshot.code = "MANAGER_DEVICE_NOT_CERTIFIED";
        snapshot.detail = "device identity fingerprint does not match this manager release";
        return false;
    }
    snapshot.available = true;
    snapshot.code = "MANAGER_READY";
    snapshot.detail = "official payload and certified device identity verified";
    return true;
}

ManagerSnapshot Manager::inspectUnlocked()
{
    ManagerSnapshot snapshot;
    snapshot.busy = busy_;
    lvgl_platform::PackageVerification verification;
    verifyBuildAndPayload(snapshot, verification);

    struct stat policyDetails {};
    if(crypto_ == nullptr || lstat(kPolicyRoot, &policyDetails) != 0) return snapshot;
    if(!S_ISDIR(policyDetails.st_mode) || policyDetails.st_uid != geteuid() ||
       (policyDetails.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        snapshot.repairRequired = true;
        snapshot.code = "MANAGER_POLICY_ROOT_UNTRUSTED";
        snapshot.detail = "anti-rollback policy root is untrusted";
        snapshot.available = false;
        return snapshot;
    }
    const auto state = lvgl_platform::load_release_state(kPolicyRoot, kPlatformAppId, *crypto_);
    if(state.status == lvgl_platform::StateStoreStatus::not_found) return snapshot;
    if(state.status != lvgl_platform::StateStoreStatus::loaded &&
       state.status != lvgl_platform::StateStoreStatus::loaded_degraded) {
        snapshot.repairRequired = true;
        snapshot.code = state.status == lvgl_platform::StateStoreStatus::uninitialized
                            ? "MANAGER_POLICY_REPAIR_REQUIRED"
                            : state.detail;
        snapshot.detail = "anti-rollback state requires repair";
        return snapshot;
    }
    const auto release = state.selected.state.current_release;
    snapshot.installed = releaseDirectoryExists(release);
    snapshot.currentVersion = "release " + std::to_string(release);
    snapshot.repairRequired = state.status == lvgl_platform::StateStoreStatus::loaded_degraded ||
                              !snapshot.installed || !currentLinkMatches(release);
    snapshot.updateAvailable = snapshot.available &&
                               verification.manifest.release_counter > release;
    if(snapshot.repairRequired) {
        snapshot.code = "MANAGER_REPAIR_REQUIRED";
        snapshot.detail = "payload, activation pointer, or redundant policy state needs repair";
    }
    return snapshot;
}

ManagerSnapshot Manager::inspect()
{
    std::lock_guard<std::mutex> guard(mutex_);
    return inspectUnlocked();
}

ManagerSnapshot Manager::installUnlocked(ManagerOperation operation)
{
    ManagerSnapshot snapshot;
    lvgl_platform::PackageVerification verification;
    if(!verifyBuildAndPayload(snapshot, verification)) return snapshot;
    auto parent = openTrustedDirectory(kPlatformParent);
    if(!parent.valid() || !cleanupRemovalTombstones(parent.get()) ||
       !ensureOwnedRoot(parent.get(), kPlatformDirectory) ||
       !ensureOwnedRoot(parent.get(), "lvgl-platform-policy")) {
        snapshot.available = false;
        snapshot.code = "MANAGER_ROOT_REJECTED";
        snapshot.detail = "fixed platform or policy root cannot be established safely";
        return snapshot;
    }
    const lvgl_platform::InstallPolicyContext context {
        std::string(manager_build::kPlatformVersion), std::string(manager_build::kSdkAbi),
        std::string(manager_build::kCertifiedProfileId),
        std::string(manager_build::kCertifiedMachine),
        {"platform.display", "platform.input", "platform.install", "platform.session"}};
    const auto result = lvgl_platform::install_official_package_with_state_root(
        kPlatformRoot, kPolicyRoot, manager_embedded::kPlatformPackage,
        manager_embedded::kPlatformPackageSize, context, *crypto_);
    if(!result.ok() || result.active_state.app_id != kPlatformAppId ||
       !activateRelease(result.active_state.current_release)) {
        snapshot.available = false;
        snapshot.code = result.ok() ? "MANAGER_ACTIVATION_FAILED" : result.detail;
        snapshot.detail = result.ok() ? "atomic current release activation failed" : result.detail;
        return snapshot;
    }
    snapshot = inspectUnlocked();
    snapshot.code = operation == ManagerOperation::install ? "MANAGER_INSTALLED"
                    : operation == ManagerOperation::repair ? "MANAGER_REPAIRED"
                                                            : "MANAGER_UPGRADED";
    snapshot.detail = result.status == lvgl_platform::InstallerStatus::installed_state_degraded
                          ? "payload installed; policy redundancy is degraded"
                          : "official platform transaction committed";
    snapshot.success = true;
    return snapshot;
}

ManagerSnapshot Manager::removeUnlocked()
{
    auto snapshot = inspectUnlocked();
    if(!snapshot.available) {
        snapshot.code = "MANAGER_REMOVE_UNAUTHORIZED";
        snapshot.detail = "this manager build or device identity is not authorized for removal";
        return snapshot;
    }
    if(!snapshot.installed) {
        snapshot.code = "MANAGER_NOT_INSTALLED";
        snapshot.detail = "no owned platform payload is installed";
        return snapshot;
    }
    if(!sessionRemovalAllowed()) {
        snapshot.code = "MANAGER_SESSION_ACTIVE";
        snapshot.detail = "a trusted stopped-session state is required before payload removal";
        return snapshot;
    }
    auto parent = openTrustedDirectory(kPlatformParent);
    auto root = parent.valid()
                    ? FileDescriptor(openat(parent.get(), kPlatformDirectory,
                                           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC))
                    : FileDescriptor();
    if(!parent.valid() || !trustedDirectory(root.get())) {
        snapshot.code = "MANAGER_ROOT_REJECTED";
        snapshot.detail = "platform root is not an owned safe directory";
        return snapshot;
    }
    std::string suffix;
    if(!randomSuffix(suffix)) {
        snapshot.code = "MANAGER_RANDOM_UNAVAILABLE";
        snapshot.detail = "secure removal transaction name could not be generated";
        return snapshot;
    }
    const std::string tombstone = ".lvgl-platform-removed-" + suffix;
    struct stat collision {};
    if(fstatat(parent.get(), tombstone.c_str(), &collision, AT_SYMLINK_NOFOLLOW) == 0 ||
       errno != ENOENT || renameat(parent.get(), kPlatformDirectory, parent.get(),
                                   tombstone.c_str()) != 0 || fsync(parent.get()) != 0) {
        snapshot.code = "MANAGER_REMOVE_ISOLATION_FAILED";
        snapshot.detail = "payload could not be isolated for removal";
        return snapshot;
    }
    std::size_t nodes = 0;
    if(!removeContents(root.get(), 0, nodes) ||
       unlinkat(parent.get(), tombstone.c_str(), AT_REMOVEDIR) != 0 || fsync(parent.get()) != 0) {
        snapshot.installed = false;
        snapshot.repairRequired = true;
        snapshot.code = "MANAGER_REMOVE_CLEANUP_DEFERRED";
        snapshot.detail = "payload was deactivated; isolated cleanup requires repair";
        return snapshot;
    }
    snapshot = inspectUnlocked();
    snapshot.installed = false;
    snapshot.repairRequired = false;
    snapshot.code = "MANAGER_REMOVED";
    snapshot.detail = "platform payload removed; anti-rollback policy and manager retained";
    snapshot.success = true;
    return snapshot;
}

ManagerSnapshot Manager::execute(ManagerOperation operation)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if(busy_) {
        auto snapshot = inspectUnlocked();
        snapshot.code = "MANAGER_BUSY";
        snapshot.detail = "another fixed manager transaction is active";
        return snapshot;
    }
    busy_ = true;
    ManagerSnapshot result = operation == ManagerOperation::remove
                                 ? removeUnlocked()
                                 : installUnlocked(operation);
    busy_ = false;
    result.busy = false;
    return result;
}
