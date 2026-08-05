#pragma once

#include <string>

struct LauncherStatus {
    bool available {false};
    bool accepted {false};
    std::string state {"idle"};
    int supervisorPid {0};
    int appPid {0};
    int result {0};
    std::string message;
};

class Launcher {
public:
    LauncherStatus probe() const;
    LauncherStatus start() const;
    LauncherStatus status() const;

private:
    LauncherStatus readStatus() const;
    bool validateRelease(std::string& error) const;
};
