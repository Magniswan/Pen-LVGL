#pragma once

#include <cstdint>
#include <mutex>
#include <string>

struct LauncherStatus {
    bool available {false};
    bool accepted {false};
    bool holeReady {false};
    bool inputReady {false};
    int logicalWidth {0};
    int logicalHeight {0};
    std::string state {"idle"};
    int sessionPid {0};
    int result {0};
    std::string message;
};

struct TouchRequest {
    std::string phase;
    std::uint32_t contactId {0};
    std::int32_t x {0};
    std::int32_t y {0};
};

class Launcher {
public:
    LauncherStatus probe();
    LauncherStatus start();
    LauncherStatus status();
    bool sendTouch(const TouchRequest& request, std::string& error);

private:
    LauncherStatus readStatus();
    bool validateRelease(std::string& error);
    bool validSessionProcess(int processId) const;

    std::mutex mutex_;
    int lastStartedPid_ {0};
    std::int32_t touchWidth_ {0};
    std::int32_t touchHeight_ {0};
    std::uint64_t touchNonce_ {0};
    std::uint64_t touchSequence_ {0};
};
