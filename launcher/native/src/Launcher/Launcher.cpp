#include "Launcher.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace {

constexpr const char* kSessiond = "/userdisk/apps/lvgl-platform/current/bin/lvgl-sessiond";
constexpr const char* kProfile = "/userdisk/apps/lvgl-platform/current/profile.env";
constexpr const char* kStatus = "/run/lvgl-platform/session.status";
constexpr const char* kTouchSocket = "/run/lvgl-platform/touch.sock";
constexpr std::uint32_t kTouchMagic = 0x4C565450U;
constexpr std::uint16_t kTouchVersion = 1;
constexpr std::size_t kTouchFrameSize = 56;

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor() { if(value_ >= 0) close(value_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ >= 0; }
private:
    int value_;
};

bool safeRootFile(int descriptor, bool executable, std::string& error)
{
    struct stat details {};
    if(descriptor < 0 || fstat(descriptor, &details) != 0 || !S_ISREG(details.st_mode)) {
        error = "required platform file is unavailable";
        return false;
    }
    if(details.st_uid != 0 || (details.st_mode & (S_IWGRP | S_IWOTH)) != 0 ||
       (executable && (details.st_mode & S_IXUSR) == 0)) {
        error = "required platform file has unsafe ownership or mode";
        return false;
    }
    return true;
}

std::string readSmallFile(const char* path)
{
    FileDescriptor input(open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    std::string error;
    if(!safeRootFile(input.get(), false, error)) return {};
    struct stat details {};
    if(fstat(input.get(), &details) != 0 || details.st_size <= 0 || details.st_size > 4096) return {};
    std::string contents(static_cast<std::size_t>(details.st_size), '\0');
    std::size_t received = 0;
    while(received < contents.size()) {
        const auto count = read(input.get(), &contents[received], contents.size() - received);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return {};
        received += static_cast<std::size_t>(count);
    }
    char trailing = 0;
    if(read(input.get(), &trailing, 1) != 0) return {};
    return contents;
}

std::map<std::string, std::string> parseStatus(const std::string& contents)
{
    std::map<std::string, std::string> values;
    std::istringstream input(contents);
    std::string line;
    while(std::getline(input, line)) {
        if(!line.empty() && line.back() == '\r') line.pop_back();
        const auto separator = line.find('=');
        if(separator == std::string::npos || separator == 0 || separator > 32 ||
           line.size() - separator - 1 > 128) continue;
        values.emplace(line.substr(0, separator), line.substr(separator + 1));
    }
    return values;
}

std::uint64_t parseUnsigned(
    const std::map<std::string, std::string>& values, const char* key) noexcept
{
    const auto found = values.find(key);
    if(found == values.end() || found->second.empty()) return 0;
    char* end = nullptr;
    errno = 0;
    const auto value = std::strtoull(found->second.c_str(), &end, 10);
    if(errno != 0 || end == found->second.c_str() || *end != '\0') return 0;
    return static_cast<std::uint64_t>(value);
}

bool flag(const std::map<std::string, std::string>& values, const char* key)
{
    const auto found = values.find(key);
    return found != values.end() && found->second == "1";
}

template <typename T>
void putLe(T value, std::uint8_t* output) noexcept
{
    using Unsigned = typename std::make_unsigned<T>::type;
    const auto converted = static_cast<Unsigned>(value);
    for(std::size_t index = 0; index < sizeof(T); ++index) {
        output[index] = static_cast<std::uint8_t>(converted >> (index * 8U));
    }
}

std::uint32_t phaseValue(const std::string& phase) noexcept
{
    if(phase == "start") return 1;
    if(phase == "move") return 2;
    if(phase == "end") return 3;
    if(phase == "cancel") return 4;
    return 0;
}

}  // namespace

bool Launcher::validSessionProcess(int processId) const
{
    if(processId <= 1 || kill(processId, 0) != 0) return false;
    const std::string link = "/proc/" + std::to_string(processId) + "/exe";
    std::array<char, 512> target {};
    const auto length = readlink(link.c_str(), target.data(), target.size() - 1);
    if(length <= 0 || static_cast<std::size_t>(length) >= target.size()) return false;
    target[static_cast<std::size_t>(length)] = '\0';
    return std::string_view(target.data(), static_cast<std::size_t>(length)) == kSessiond;
}

LauncherStatus Launcher::readStatus()
{
    LauncherStatus status;
    const auto values = parseStatus(readSmallFile(kStatus));
    const auto processId = parseUnsigned(values, "SESSION_PID");
    const auto nonce = parseUnsigned(values, "SESSION_NONCE");
    if(processId > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) || nonce == 0 ||
       !validSessionProcess(static_cast<int>(processId))) {
        if(lastStartedPid_ > 1 && validSessionProcess(lastStartedPid_)) {
            status.state = "launching";
            status.sessionPid = lastStartedPid_;
        } else if(lastStartedPid_ > 1) {
            int childStatus = 0;
            waitpid(lastStartedPid_, &childStatus, WNOHANG);
            lastStartedPid_ = 0;
        }
        return status;
    }
    status.sessionPid = static_cast<int>(processId);
    const auto state = values.find("STATE");
    if(state != values.end() && (state->second == "starting" || state->second == "ready" ||
                                 state->second == "error")) {
        status.state = state->second;
    }
    status.result = static_cast<int>(std::min<std::uint64_t>(
        parseUnsigned(values, "RESULT"), std::numeric_limits<int>::max()));
    status.holeReady = status.state == "ready" && flag(values, "HOLE_READY");
    const auto logicalWidth = parseUnsigned(values, "LOGICAL_WIDTH");
    const auto logicalHeight = parseUnsigned(values, "LOGICAL_HEIGHT");
    const bool dimensionsValid = logicalWidth == static_cast<std::uint64_t>(touchWidth_) &&
                                 logicalHeight == static_cast<std::uint64_t>(touchHeight_) &&
                                 touchWidth_ > 0 && touchHeight_ > 0;
    status.inputReady = status.holeReady && dimensionsValid && flag(values, "INPUT_READY");
    status.logicalWidth = touchWidth_;
    status.logicalHeight = touchHeight_;
    if(touchNonce_ != nonce) {
        touchNonce_ = nonce;
        touchSequence_ = 0;
    }
    return status;
}

bool Launcher::validateRelease(std::string& error)
{
    touchWidth_ = 0;
    touchHeight_ = 0;
    FileDescriptor executable(open(kSessiond, O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if(!safeRootFile(executable.get(), true, error)) return false;
    const auto profile = parseStatus(readSmallFile(kProfile));
    const auto width = parseUnsigned(profile, "LOGICAL_WIDTH");
    const auto height = parseUnsigned(profile, "LOGICAL_HEIGHT");
    if(width < 240 || width > 4096 || height < 80 || height > 2048 ||
       (!flag(profile, "HOLE_SESSION_CERTIFIED") && !flag(profile, "PERSONAL_UNBOUND"))) {
        error = "installed platform profile does not permit Falcon hole sessions";
        return false;
    }
    touchWidth_ = static_cast<std::int32_t>(width);
    touchHeight_ = static_cast<std::int32_t>(height);
    return true;
}

LauncherStatus Launcher::probe()
{
    std::lock_guard<std::mutex> guard(mutex_);
    std::string error;
    LauncherStatus result;
    result.available = validateRelease(error);
    if(result.available) result = readStatus();
    result.available = error.empty();
    result.logicalWidth = touchWidth_;
    result.logicalHeight = touchHeight_;
    result.message = result.available ? "ready" : error;
    return result;
}

LauncherStatus Launcher::status()
{
    return probe();
}

LauncherStatus Launcher::start()
{
    std::lock_guard<std::mutex> guard(mutex_);
    std::string error;
    LauncherStatus result;
    if(!validateRelease(error)) {
        result.message = error;
        return result;
    }
    result = readStatus();
    result.logicalWidth = touchWidth_;
    result.logicalHeight = touchHeight_;
    FileDescriptor executable(open(kSessiond, O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if(!safeRootFile(executable.get(), true, error)) {
        result.message = error;
        return result;
    }
    result.available = true;
    if(result.sessionPid > 1 && (result.state == "launching" || result.state == "starting" ||
                                 result.state == "ready")) {
        result.state = result.holeReady ? "ready" : "launching";
        result.message = "LVGL session is already active";
        return result;
    }

    const pid_t child = fork();
    if(child < 0) {
        result.message = std::string("fork failed: ") + std::strerror(errno);
        return result;
    }
    if(child == 0) {
        if(setsid() < 0 || chdir("/") != 0) _exit(126);
        const int nullDescriptor = open("/dev/null", O_RDWR | O_CLOEXEC);
        if(nullDescriptor >= 0) {
            dup2(nullDescriptor, STDIN_FILENO);
            dup2(nullDescriptor, STDOUT_FILENO);
            dup2(nullDescriptor, STDERR_FILENO);
            if(nullDescriptor > STDERR_FILENO) close(nullDescriptor);
        }
        const long openLimit = sysconf(_SC_OPEN_MAX);
        const int lastDescriptor = openLimit > 0
            ? static_cast<int>(std::min<long>(openLimit, 65536L))
            : 1024;
        for(int descriptor = STDERR_FILENO + 1; descriptor < lastDescriptor; ++descriptor) {
            if(descriptor != executable.get()) close(descriptor);
        }
        char executableName[] = "lvgl-sessiond";
        char holeMode[] = "--falcon-hole";
        char path[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin";
        char* const arguments[] = {executableName, holeMode, nullptr};
        char* const environment[] = {path, nullptr};
        fexecve(executable.get(), arguments, environment);
        _exit(127);
    }

    lastStartedPid_ = static_cast<int>(child);
    touchNonce_ = 0;
    touchSequence_ = 0;
    result.accepted = true;
    result.state = "launching";
    result.sessionPid = lastStartedPid_;
    result.message = "launch accepted";
    return result;
}

bool Launcher::sendTouch(const TouchRequest& request, std::string& error)
{
    std::lock_guard<std::mutex> guard(mutex_);
    const auto status = readStatus();
    const auto phase = phaseValue(request.phase);
    if(!status.inputReady || touchNonce_ == 0) {
        error = "LVGL touch channel is not ready";
        return false;
    }
    if(phase == 0 || request.contactId > 31 || touchWidth_ <= 0 || touchHeight_ <= 0 ||
       request.x < 0 || request.x >= touchWidth_ || request.y < 0 || request.y >= touchHeight_ ||
       touchSequence_ == std::numeric_limits<std::uint64_t>::max()) {
        error = "touch frame is invalid";
        return false;
    }
    struct timespec now {};
    if(clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        error = "monotonic clock is unavailable";
        return false;
    }
    const auto monotonicUs = static_cast<std::uint64_t>(now.tv_sec) * 1000000ULL +
                             static_cast<std::uint64_t>(now.tv_nsec / 1000);
    if(monotonicUs == 0) {
        error = "monotonic timestamp is invalid";
        return false;
    }
    struct stat socketStatus {};
    if(lstat(kTouchSocket, &socketStatus) != 0 || !S_ISSOCK(socketStatus.st_mode) ||
       socketStatus.st_uid != 0 || (socketStatus.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        error = "touch socket is untrusted";
        return false;
    }
    FileDescriptor socketDescriptor(socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if(!socketDescriptor.valid()) {
        error = "touch socket cannot be opened";
        return false;
    }
    struct sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, kTouchSocket, sizeof(address.sun_path) - 1);
    if(connect(socketDescriptor.get(), reinterpret_cast<struct sockaddr*>(&address),
               sizeof(address)) != 0) {
        error = "touch socket cannot be connected";
        return false;
    }

    std::array<std::uint8_t, kTouchFrameSize> frame {};
    putLe<std::uint32_t>(kTouchMagic, frame.data());
    putLe<std::uint16_t>(kTouchVersion, frame.data() + 4);
    putLe<std::uint16_t>(static_cast<std::uint16_t>(kTouchFrameSize), frame.data() + 6);
    putLe<std::uint64_t>(touchNonce_, frame.data() + 8);
    putLe<std::uint64_t>(++touchSequence_, frame.data() + 16);
    putLe<std::uint64_t>(monotonicUs, frame.data() + 24);
    putLe<std::uint32_t>(phase, frame.data() + 32);
    putLe<std::uint32_t>(request.contactId, frame.data() + 36);
    putLe<std::int32_t>(request.x, frame.data() + 40);
    putLe<std::int32_t>(request.y, frame.data() + 44);
    const auto sent = send(socketDescriptor.get(), frame.data(), frame.size(), MSG_NOSIGNAL);
    if(sent != static_cast<ssize_t>(frame.size())) {
        error = "touch frame was not delivered";
        return false;
    }
    return true;
}
