#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace lvgl_platform {
class CryptoProvider;
struct PackageVerification;
}

struct ManagerSnapshot {
    bool success {false};
    bool available {false};
    bool payloadAvailable {false};
    bool installed {false};
    bool repairRequired {false};
    bool updateAvailable {false};
    bool busy {false};
    std::string currentVersion;
    std::string payloadVersion;
    std::string profileId;
    std::string code {"MANAGER_UNAVAILABLE"};
    std::string detail;
};

enum class ManagerOperation : std::uint8_t { install, repair, upgrade, remove };

class Manager {
public:
    Manager();
    ~Manager();
    ManagerSnapshot inspect();
    ManagerSnapshot execute(ManagerOperation operation);

private:
    ManagerSnapshot inspectUnlocked();
    ManagerSnapshot installUnlocked(ManagerOperation operation);
    ManagerSnapshot removeUnlocked();
    bool verifyBuildAndPayload(
        ManagerSnapshot& snapshot, lvgl_platform::PackageVerification& verification);

    std::mutex mutex_;
    std::unique_ptr<lvgl_platform::CryptoProvider> crypto_;
    bool busy_ {false};
};
