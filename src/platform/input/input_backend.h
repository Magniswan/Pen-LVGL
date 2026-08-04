#pragma once

#include "platform/device_profile/device_profile.h"

#include <cstdint>
#include <memory>

namespace dictpen {

struct PointerState {
    Point point {0, 0};
    bool pressed {false};
    bool changed {false};
    int32_t raw_x {0};
    int32_t raw_y {0};
};

class InputBackend {
public:
    explicit InputBackend(const DeviceProfile& profile);
    ~InputBackend();

    InputBackend(const InputBackend&) = delete;
    InputBackend& operator=(const InputBackend&) = delete;

    bool open();
    void close();
    void poll();
    PointerState state() const;
    const char* last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dictpen
