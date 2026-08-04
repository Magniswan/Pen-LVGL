#include "platform/input/input_backend.h"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace dictpen {

struct InputBackend::Impl {
    explicit Impl(const DeviceProfile& value) : profile(value) {}

    DeviceProfile profile;
    int fd {-1};
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
    }
}

void InputBackend::poll()
{
    if(!impl_ || impl_->fd < 0) return;
    impl_->pointer.changed = false;
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
