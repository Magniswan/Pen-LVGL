#include "platform/input/input_backend.h"

#include "lvgl_platform/touch_protocol.h"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace dictpen {

struct InputBackend::Impl {
    explicit Impl(const DeviceProfile& value) : profile(value) {}

    DeviceProfile profile;
    int fd {-1};
    bool forwarded {false};
    int32_t min_x {0};
    int32_t max_x {480};
    int32_t min_y {0};
    int32_t max_y {960};
    PointerState pointer;
    std::string error;

    bool fail(const char* operation)
    {
        error = std::string(operation) + ": " + std::strerror(errno);
        return false;
    }

    bool read_axis(unsigned int code, int32_t& minimum, int32_t& maximum)
    {
        input_absinfo info {};
        if(ioctl(fd, EVIOCGABS(code), &info) != 0) return false;
        minimum = info.minimum;
        maximum = info.maximum;
        return maximum > minimum;
    }

    bool open_forwarded()
    {
        const char* value = std::getenv("LVGL_TOUCH_FD");
        if(value == nullptr || *value == '\0') return false;
        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value, &end, 10);
        if(errno != 0 || end == value || *end != '\0' || parsed < 3 ||
           parsed > std::numeric_limits<int>::max()) {
            errno = EINVAL;
            return false;
        }
        struct stat details {};
        int type = 0;
        socklen_t type_size = sizeof(type);
        if(::fstat(static_cast<int>(parsed), &details) != 0 || !S_ISSOCK(details.st_mode) ||
           ::getsockopt(static_cast<int>(parsed), SOL_SOCKET, SO_TYPE, &type, &type_size) != 0 ||
           type != SOCK_DGRAM) {
            errno = EBADF;
            return false;
        }
        const int flags = ::fcntl(static_cast<int>(parsed), F_GETFL);
        if(flags < 0 || ::fcntl(static_cast<int>(parsed), F_SETFL, flags | O_NONBLOCK) != 0) {
            return false;
        }
        fd = static_cast<int>(parsed);
        forwarded = true;
        return true;
    }

    void poll_forwarded()
    {
        std::array<std::uint8_t, lvgl_platform::kTouchFrameWireSize> bytes {};
        while(true) {
            const auto received = ::recv(fd, bytes.data(), bytes.size(), MSG_DONTWAIT | MSG_TRUNC);
            if(received < 0 && errno == EINTR) continue;
            if(received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if(received <= 0) break;
            const auto decoded = lvgl_platform::decode_touch_frame(
                bytes.data(), static_cast<std::size_t>(received),
                profile.logical_width, profile.logical_height);
            if(!decoded.ok() || decoded.frame.contact_id != 0) continue;
            pointer.raw_x = decoded.frame.x;
            pointer.raw_y = decoded.frame.y;
            pointer.point = {decoded.frame.x, decoded.frame.y};
            pointer.pressed = decoded.frame.phase == lvgl_platform::TouchPhase::start ||
                              decoded.frame.phase == lvgl_platform::TouchPhase::move;
            pointer.changed = true;
        }
    }

    Point map_raw(int32_t raw_x, int32_t raw_y) const
    {
        const int32_t physical_x = min_x == max_x
                                       ? raw_x
                                       : (raw_x - min_x) * profile.physical_width /
                                             (max_x - min_x);
        const int32_t physical_y = min_y == max_y
                                       ? raw_y
                                       : (raw_y - min_y) * profile.physical_height /
                                             (max_y - min_y);
        return raw_touch_to_logical({physical_x, physical_y}, profile);
    }
};

InputBackend::InputBackend(const DeviceProfile& profile)
    : impl_(std::make_unique<Impl>(profile))
{
}

InputBackend::~InputBackend()
{
    close();
}

bool InputBackend::open()
{
    if(impl_->fd >= 0) return true;
    const bool forwarded_requested = std::getenv("LVGL_TOUCH_FD") != nullptr;
    if(impl_->open_forwarded()) return true;
    if(forwarded_requested) return impl_->fail("forwarded touch descriptor");
    for(int index = 0; index < 32; ++index) {
        const std::string path = "/dev/input/event" + std::to_string(index);
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if(fd < 0) continue;
        char name[128] {};
        if(ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 &&
           std::strcmp(name, impl_->profile.touch_name) == 0) {
            impl_->fd = fd;
            if(!impl_->read_axis(ABS_MT_POSITION_X, impl_->min_x, impl_->max_x) ||
               !impl_->read_axis(ABS_MT_POSITION_Y, impl_->min_y, impl_->max_y)) {
                impl_->min_x = 0;
                impl_->max_x = impl_->profile.physical_width;
                impl_->min_y = 0;
                impl_->max_y = impl_->profile.physical_height;
            }
            return true;
        }
        ::close(fd);
    }
    errno = ENODEV;
    return impl_->fail("touch device");
}

void InputBackend::close()
{
    if(impl_ && impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
        impl_->forwarded = false;
    }
}

void InputBackend::poll()
{
    if(!impl_ || impl_->fd < 0) return;
    impl_->pointer.changed = false;
    if(impl_->forwarded) {
        impl_->poll_forwarded();
        return;
    }
    input_event event {};
    while(read(impl_->fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event))) {
        if(event.type == EV_ABS) {
            if(event.code == ABS_MT_TRACKING_ID) {
                impl_->pointer.pressed = event.value >= 0;
                impl_->pointer.changed = true;
            }
            else if(event.code == ABS_MT_POSITION_X || event.code == ABS_MT_POSITION_Y) {
                if(event.code == ABS_MT_POSITION_X) impl_->pointer.raw_x = event.value;
                if(event.code == ABS_MT_POSITION_Y) impl_->pointer.raw_y = event.value;
                impl_->pointer.point = impl_->map_raw(impl_->pointer.raw_x, impl_->pointer.raw_y);
                impl_->pointer.changed = true;
            }
        }
        else if(event.type == EV_KEY && event.code == BTN_TOUCH) {
            impl_->pointer.pressed = event.value != 0;
            impl_->pointer.changed = true;
        }
    }
}

PointerState InputBackend::state() const
{
    return impl_ ? impl_->pointer : PointerState {};
}

const char* InputBackend::last_error() const
{
    return impl_ ? impl_->error.c_str() : "backend not initialized";
}

}  // namespace dictpen
