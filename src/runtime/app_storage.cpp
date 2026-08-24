#include "runtime/app_storage.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dictpen {
namespace {

#if !defined(_WIN32)

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor() { if(value_ >= 0) ::close(value_); }
    int get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ >= 0; }

private:
    int value_;
};

int inherited_directory() noexcept
{
    const char* value = std::getenv("LVGL_APP_STORAGE_FD");
    if(value == nullptr || *value == '\0') return -1;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if(errno != 0 || end == value || *end != '\0' || parsed < 0 ||
       parsed > std::numeric_limits<int>::max()) {
        return -1;
    }
    struct stat details {};
    const int descriptor = static_cast<int>(parsed);
    if(::fstat(descriptor, &details) != 0 || !S_ISDIR(details.st_mode) ||
       details.st_uid != 0 || (details.st_mode & 0777) != 0700) {
        return -1;
    }
    return descriptor;
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

bool random_suffix(std::string& output) noexcept
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

#endif

}  // namespace

AppStorage::AppStorage() noexcept
{
#if !defined(_WIN32)
    const int inherited = inherited_directory();
    if(inherited >= 0) directory_fd_ = ::fcntl(inherited, F_DUPFD_CLOEXEC, 3);
#endif
}

AppStorage::~AppStorage()
{
#if !defined(_WIN32)
    if(directory_fd_ >= 0) ::close(directory_fd_);
#endif
}

bool AppStorage::valid_record_name(std::string_view value) noexcept
{
    if(value.empty() || value.size() > 64 || value.front() == '.') return false;
    for(const char character : value) {
        const bool allowed = (character >= 'a' && character <= 'z') ||
                             (character >= '0' && character <= '9') ||
                             character == '.' || character == '_' || character == '-';
        if(!allowed) return false;
    }
    return value.find("..") == std::string_view::npos;
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
    if(directory_fd_ < 0 || !valid_record_name(record) || maximum_size == 0) return false;
    const std::string name(record);
    FileDescriptor input(::openat(
        directory_fd_, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat details {};
    if(!input.valid() || ::fstat(input.get(), &details) != 0 || !S_ISREG(details.st_mode) ||
       details.st_uid != 0 || details.st_nlink != 1 || (details.st_mode & 0777) != 0600 ||
       details.st_size <= 0 || static_cast<std::uint64_t>(details.st_size) > maximum_size) {
        return false;
    }
    output.assign(static_cast<std::size_t>(details.st_size), 0);
    std::size_t received = 0;
    while(received < output.size()) {
        const auto count = ::read(input.get(), output.data() + received, output.size() - received);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) {
            output.clear();
            return false;
        }
        received += static_cast<std::size_t>(count);
    }
    std::uint8_t trailing = 0;
    if(::read(input.get(), &trailing, 1) != 0) {
        output.clear();
        return false;
    }
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
    if(directory_fd_ < 0 || !valid_record_name(record) || data == nullptr || size == 0 ||
       size > maximum_size) {
        return false;
    }
    std::string suffix;
    if(!random_suffix(suffix)) return false;
    const std::string name(record);
    const std::string temporary = ".write-" + suffix;
    FileDescriptor output(::openat(
        directory_fd_, temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if(!output.valid() || !write_all(output.get(), data, size) || ::fsync(output.get()) != 0 ||
       ::renameat(directory_fd_, temporary.c_str(), directory_fd_, name.c_str()) != 0 ||
       ::fsync(directory_fd_) != 0) {
        ::unlinkat(directory_fd_, temporary.c_str(), 0);
        return false;
    }
    return true;
#endif
}

}  // namespace dictpen
