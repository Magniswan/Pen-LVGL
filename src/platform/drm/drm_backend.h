#pragma once

#include "platform/device_profile/device_profile.h"

#include <cstdint>
#include <memory>

namespace dictpen {

class DrmBackend {
public:
    explicit DrmBackend(const DeviceProfile& profile);
    ~DrmBackend();

    DrmBackend(const DrmBackend&) = delete;
    DrmBackend& operator=(const DrmBackend&) = delete;

    bool open();
    bool present_logical(const uint32_t* pixels, int32_t width, int32_t height);
    void close();

    bool is_open() const;
    bool has_presented_frame() const;
    const char* last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dictpen
