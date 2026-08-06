#include "Launcher.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace {

constexpr const char* kProfileId = "OVERHEAD_Y01_SKU_CHN_PRO";
constexpr const char* kAppRoot = "/userdisk/apps/lvgl-poc/current";
constexpr const char* kSupervisor = "/userdisk/apps/lvgl-poc/current/bin/lvgl-supervisor.sh";
constexpr const char* kApp = "/userdisk/apps/lvgl-poc/current/bin/lvgl_session";
constexpr const char* kManifest = "/userdisk/apps/lvgl-poc/current/manifest.env";
constexpr const char* kLock = "/run/lvgl-poc/lock";
constexpr const char* kStatus = "/run/lvgl-poc/status.env";

std::map<std::string, std::string> readKeyValues(const char* path)
{
    std::map<std::string, std::string> values;
    std::ifstream input(path);
    std::string line;
    while(std::getline(input, line)) {
        if(!line.empty() && line.back() == '\r') line.pop_back();
        const size_t separator = line.find('=');
        if(separator == std::string::npos) continue;
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return values;
}

int parseInteger(const std::map<std::string, std::string>& values, const char* key)
{
    const auto found = values.find(key);
    if(found == values.end()) return 0;
    char* end = nullptr;
    const long result = std::strtol(found->second.c_str(), &end, 10);
    if(end == found->second.c_str() || *end != '\0' || result < 0 || result > 2147483647L) return 0;
    return static_cast<int>(result);
}

bool validateFixedFile(const char* path, bool executable, std::string& error)
{
    struct stat details {};
    if(lstat(path, &details) != 0) {
        error = std::string(path) + ": " + std::strerror(errno);
        return false;
    }
    if(!S_ISREG(details.st_mode)) {
        error = std::string(path) + " is not a regular file";
        return false;
    }
    if(details.st_uid != 0 || (details.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        error = std::string(path) + " has unsafe ownership or mode";
        return false;
    }
    if(executable && access(path, X_OK) != 0) {
        error = std::string(path) + " is not executable";
        return false;
    }
    return true;
}

}  // namespace

LauncherStatus Launcher::readStatus() const
{
    LauncherStatus status;
    const auto values = readKeyValues(kStatus);
    if(values.empty()) return status;
    const auto state = values.find("STATE");
    if(state != values.end() && !state->second.empty()) status.state = state->second;
    status.supervisorPid = parseInteger(values, "SUPERVISOR_PID");
    status.appPid = parseInteger(values, "APP_PID");
    status.result = parseInteger(values, "RESULT");
    return status;
}

bool Launcher::validateRelease(std::string& error) const
{
    struct stat root {};
    if(stat(kAppRoot, &root) != 0 || !S_ISDIR(root.st_mode)) {
        error = "LVGL release is not installed";
        return false;
    }
    if(!validateFixedFile(kSupervisor, true, error) || !validateFixedFile(kApp, true, error) ||
       !validateFixedFile(kManifest, false, error)) {
        return false;
    }
    const auto manifest = readKeyValues(kManifest);
    const auto profile = manifest.find("PROFILE_ID");
    if(profile == manifest.end() || profile->second != kProfileId) {
        error = "LVGL release profile does not match this launcher";
        return false;
    }
    return true;
}

LauncherStatus Launcher::probe() const
{
    LauncherStatus result = readStatus();
    std::string error;
    result.available = validateRelease(error);
    result.message = result.available ? "ready" : error;
    return result;
}

LauncherStatus Launcher::status() const
{
    LauncherStatus result = readStatus();
    std::string error;
    result.available = validateRelease(error);
    result.message = result.available ? "ready" : error;
    return result;
}

LauncherStatus Launcher::start() const
{
    LauncherStatus result = readStatus();
    std::string error;
    if(!validateRelease(error)) {
        result.message = error;
        return result;
    }
    result.available = true;
    if(access(kLock, F_OK) == 0) {
        result.state = "already_running";
        result.message = "LVGL is already launching or running";
        return result;
    }

    const pid_t child = fork();
    if(child < 0) {
        result.message = std::string("fork failed: ") + std::strerror(errno);
        return result;
    }
    if(child == 0) {
        if(setsid() < 0) _exit(126);
        if(chdir("/") != 0) _exit(126);
        const int nullFd = open("/dev/null", O_RDWR | O_CLOEXEC);
        if(nullFd >= 0) {
            dup2(nullFd, STDIN_FILENO);
            dup2(nullFd, STDOUT_FILENO);
            dup2(nullFd, STDERR_FILENO);
            if(nullFd > STDERR_FILENO) close(nullFd);
        }
        char supervisor[] = "/userdisk/apps/lvgl-poc/current/bin/lvgl-supervisor.sh";
        char shell[] = "/bin/sh";
        char run[] = "run";
        char path[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin";
        char* const arguments[] = {shell, supervisor, run, nullptr};
        char* const environment[] = {path, nullptr};
        execve(shell, arguments, environment);
        _exit(127);
    }

    result.accepted = true;
    result.state = "launching";
    result.supervisorPid = static_cast<int>(child);
    result.message = "launch accepted";
    return result;
}
