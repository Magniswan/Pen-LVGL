#include "lvgl_platform/inbox_service.h"

#include "lvgl_platform/session_control.h"
#include "lvgl_platform/trust_store.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#if !defined(_WIN32)
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace lvgl_platform {
namespace {

constexpr std::size_t kMaximumPackageSize = 256U * 1024U * 1024U + 96U;
constexpr std::size_t kMaximumInboxEntries = 128;

InboxScanResult scan_failure(InboxStatus status, const char* detail)
{
    return {status, detail, {}};
}

bool reserved_application_id(std::string_view app_id) noexcept
{
    return app_id == "top.lvgl.platform" || app_id == "top.lvgl.desktop" ||
           app_id == "top.lvgl.installer";
}

bool valid_token(std::string_view token) noexcept
{
    return token.size() == 128 && std::all_of(token.begin(), token.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

std::string hex_digest(const Sha512Digest& digest)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(digest.size() * 2);
    for(const auto value : digest) {
        output.push_back(hex[value >> 4U]);
        output.push_back(hex[value & 0x0fU]);
    }
    return output;
}

#if !defined(_WIN32)

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor() { if(value_ >= 0) ::close(value_); }
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

struct DirectoryCloser {
    void operator()(DIR* directory) const noexcept
    {
        if(directory != nullptr) ::closedir(directory);
    }
};

struct RawCandidate {
    InboxCandidate public_value;
    std::vector<std::uint8_t> bytes;
    PackageVerification verification;
    Sha512Digest digest {};
};

bool trusted_directory(int descriptor) noexcept
{
    struct stat details {};
    return descriptor >= 0 && ::fstat(descriptor, &details) == 0 && S_ISDIR(details.st_mode) &&
           details.st_uid == 0 && (details.st_mode & 0022) == 0;
}

FileDescriptor open_directory_at(int parent, const char* name) noexcept
{
    return FileDescriptor(::openat(
        parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
}

FileDescriptor ensure_directory_at(int parent, const char* name) noexcept
{
    bool created = false;
    if(::mkdirat(parent, name, 0700) == 0) created = true;
    else if(errno != EEXIST) return FileDescriptor();
    auto directory = open_directory_at(parent, name);
    if(!trusted_directory(directory.get())) return FileDescriptor();
    if(created && (::fsync(directory.get()) != 0 || ::fsync(parent) != 0)) {
        return FileDescriptor();
    }
    return directory;
}

FileDescriptor open_apps_parent() noexcept
{
    FileDescriptor root(::open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if(!trusted_directory(root.get())) return FileDescriptor();
    auto userdisk = open_directory_at(root.get(), "userdisk");
    if(!trusted_directory(userdisk.get())) return FileDescriptor();
    auto apps = open_directory_at(userdisk.get(), "apps");
    return trusted_directory(apps.get()) ? std::move(apps) : FileDescriptor {};
}

bool read_candidate(int inbox, const std::string& name, std::vector<std::uint8_t>& output)
{
    FileDescriptor file(::openat(inbox, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat before {};
    if(!file.valid() || ::fstat(file.get(), &before) != 0 || !S_ISREG(before.st_mode) ||
       before.st_uid != 0 || before.st_nlink != 1 || (before.st_mode & 0022) != 0 ||
       before.st_size <= 0 || static_cast<std::uint64_t>(before.st_size) > kMaximumPackageSize) {
        return false;
    }
    output.resize(static_cast<std::size_t>(before.st_size));
    std::size_t received = 0;
    while(received < output.size()) {
        const auto count = ::read(file.get(), output.data() + received, output.size() - received);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return false;
        received += static_cast<std::size_t>(count);
    }
    std::uint8_t trailing = 0;
    struct stat after {};
    return ::read(file.get(), &trailing, 1) == 0 && ::fstat(file.get(), &after) == 0 &&
           before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
           before.st_size == after.st_size && before.st_mtime == after.st_mtime &&
           before.st_ctime == after.st_ctime;
}

InboxStatus raw_candidates(
    const InstallPolicyContext& context, const CryptoProvider& crypto,
    std::vector<RawCandidate>& output)
{
    auto parent = open_apps_parent();
    auto inbox = parent.valid() ? open_directory_at(parent.get(), "lvgl-inbox") : FileDescriptor {};
    if(!trusted_directory(inbox.get())) return InboxStatus::root_untrusted;
    const int duplicate = ::dup(inbox.get());
    if(duplicate < 0) return InboxStatus::io_error;
    std::unique_ptr<DIR, DirectoryCloser> directory(::fdopendir(duplicate));
    if(!directory) {
        ::close(duplicate);
        return InboxStatus::io_error;
    }
    std::vector<std::string> names;
    errno = 0;
    while(const auto* entry = ::readdir(directory.get())) {
        const std::string_view name(entry->d_name);
        if(name != "." && name != "..") names.emplace_back(name);
        if(names.size() > kMaximumInboxEntries) return InboxStatus::io_error;
    }
    if(errno != 0) return InboxStatus::io_error;
    std::sort(names.begin(), names.end());
    const auto trust = OfficialTrustStore::compiled();
    for(const auto& name : names) {
        RawCandidate candidate;
        if(!read_candidate(inbox.get(), name, candidate.bytes) ||
           !crypto.sha512(candidate.bytes.data(), candidate.bytes.size(), candidate.digest)) {
            continue;
        }
        candidate.public_value.token = hex_digest(candidate.digest);
        candidate.public_value.package_size = candidate.bytes.size();
        candidate.verification = trust.verify(
            candidate.bytes.data(), candidate.bytes.size(), crypto);
        candidate.public_value.detail = candidate.verification.detail;
        if(candidate.verification.ok()) {
            const auto& manifest = candidate.verification.manifest;
            candidate.public_value.app_id = manifest.app_id;
            candidate.public_value.name = manifest.name;
            candidate.public_value.version = manifest.version;
            candidate.public_value.release_counter = manifest.release_counter;
            candidate.public_value.security_epoch = manifest.security_epoch;
            if(reserved_application_id(manifest.app_id)) {
                candidate.public_value.detail = "INBOX_RESERVED_APPLICATION_ID";
            } else {
                std::optional<ApplicationHighWaterMark> high_water;
                const auto state = load_release_state(
                    kApplicationPolicyRoot, manifest.app_id, crypto);
                if(state.status == StateStoreStatus::loaded ||
                   state.status == StateStoreStatus::loaded_degraded) {
                    const auto& active = state.selected.state;
                    high_water = ApplicationHighWaterMark {
                        active.app_id, active.signing_key_id, active.high_release,
                        active.high_security_epoch, active.high_digest};
                } else if(state.status != StateStoreStatus::not_found &&
                          state.status != StateStoreStatus::uninitialized) {
                    candidate.public_value.detail = "INBOX_ANTI_ROLLBACK_STATE_UNAVAILABLE";
                    output.push_back(std::move(candidate));
                    continue;
                }
                const auto policy = evaluate_install_policy(
                    candidate.verification, candidate.digest, context, high_water);
                candidate.public_value.detail = policy.detail;
                candidate.public_value.installable = policy.allowed();
            }
        }
        output.push_back(std::move(candidate));
    }
    std::sort(output.begin(), output.end(), [](const auto& left, const auto& right) {
        return left.public_value.token < right.public_value.token;
    });
    output.erase(std::unique(output.begin(), output.end(), [](const auto& left, const auto& right) {
        return left.public_value.token == right.public_value.token;
    }), output.end());
    return output.empty() ? InboxStatus::empty : InboxStatus::ready;
}

#endif

}  // namespace

InboxStatus prepare_application_storage() noexcept
{
#if defined(_WIN32)
    return InboxStatus::unsupported_platform;
#else
    auto parent = open_apps_parent();
    if(!parent.valid()) return InboxStatus::root_untrusted;
    auto payload = ensure_directory_at(parent.get(), "lvgl-apps");
    auto policy = ensure_directory_at(parent.get(), "lvgl-app-policy");
    auto inbox = ensure_directory_at(parent.get(), "lvgl-inbox");
    return payload.valid() && policy.valid() && inbox.valid() ? InboxStatus::ready
                                                             : InboxStatus::io_error;
#endif
}

InboxScanResult scan_official_inbox(
    const InstallPolicyContext& policy_context, const CryptoProvider& crypto)
{
#if defined(_WIN32)
    (void)policy_context;
    (void)crypto;
    return scan_failure(InboxStatus::unsupported_platform, "INBOX_POSIX_REQUIRED");
#else
    std::vector<RawCandidate> candidates;
    const auto status = raw_candidates(policy_context, crypto, candidates);
    if(status != InboxStatus::ready && status != InboxStatus::empty) {
        return scan_failure(status, status == InboxStatus::root_untrusted
                                        ? "INBOX_ROOT_UNTRUSTED"
                                        : "INBOX_SCAN_FAILED");
    }
    InboxScanResult result;
    result.status = status;
    result.detail = status == InboxStatus::empty ? "INBOX_EMPTY" : "INBOX_READY";
    result.candidates.reserve(candidates.size());
    for(auto& candidate : candidates) {
        result.candidates.push_back(std::move(candidate.public_value));
    }
    return result;
#endif
}

InboxInstallResult install_official_inbox_candidate(
    const std::string& token, const InstallPolicyContext& policy_context,
    const CryptoProvider& crypto)
{
    InboxInstallResult result;
    if(!valid_token(token)) {
        result.status = InboxStatus::invalid_token;
        result.detail = "INBOX_TOKEN_INVALID";
        return result;
    }
#if defined(_WIN32)
    (void)policy_context;
    (void)crypto;
    result.status = InboxStatus::unsupported_platform;
    result.detail = "INBOX_POSIX_REQUIRED";
    return result;
#else
    std::vector<RawCandidate> candidates;
    const auto scan = raw_candidates(policy_context, crypto, candidates);
    if(scan != InboxStatus::ready) {
        result.status = scan == InboxStatus::empty ? InboxStatus::candidate_not_found : scan;
        result.detail = scan == InboxStatus::root_untrusted ? "INBOX_ROOT_UNTRUSTED"
                                                             : "INBOX_CANDIDATE_NOT_FOUND";
        return result;
    }
    const auto candidate = std::find_if(candidates.begin(), candidates.end(), [&token](const auto& value) {
        return value.public_value.token == token;
    });
    if(candidate == candidates.end()) {
        result.status = InboxStatus::candidate_not_found;
        result.detail = "INBOX_CANDIDATE_NOT_FOUND";
        return result;
    }
    if(!candidate->public_value.installable || !candidate->verification.ok() ||
       reserved_application_id(candidate->verification.manifest.app_id)) {
        result.status = InboxStatus::candidate_rejected;
        result.detail = candidate->public_value.detail;
        return result;
    }
    result.installer = install_official_package_with_state_root(
        kApplicationPayloadRoot, kApplicationPolicyRoot, candidate->bytes.data(),
        candidate->bytes.size(), policy_context, crypto);
    result.status = result.installer.ok() ? InboxStatus::ready : InboxStatus::candidate_rejected;
    result.detail = result.installer.detail;
    return result;
#endif
}

}  // namespace lvgl_platform
