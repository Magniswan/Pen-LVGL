#include "platform/device_profile/device_profile.h"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

int open_touch(const char* expected_name)
{
    for(int index = 0; index < 32; ++index) {
        const std::string path = "/dev/input/event" + std::to_string(index);
        const int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
        if(fd < 0) continue;
        char name[128] {};
        if(ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && std::strcmp(name, expected_name) == 0) {
            std::cout << "inject.device=" << path << '\n';
            return fd;
        }
        close(fd);
    }
    return -1;
}

bool emit(int fd, uint16_t type, uint16_t code, int32_t value)
{
    input_event event {};
    event.type = type;
    event.code = code;
    event.value = value;
    return write(fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event));
}

bool point(int fd, int32_t logical_x, int32_t logical_y, const dictpen::DeviceProfile& profile)
{
    const int32_t raw_x = std::clamp(logical_y + profile.touch_cross_axis_offset,
                                     int32_t {0}, profile.physical_width);
    const int32_t raw_y = std::clamp(profile.physical_height - 1 - logical_x,
                                     int32_t {0}, profile.physical_height);
    return emit(fd, EV_ABS, ABS_MT_POSITION_X, raw_x) &&
           emit(fd, EV_ABS, ABS_MT_POSITION_Y, raw_y) &&
           emit(fd, EV_SYN, SYN_REPORT, 0);
}

bool contact(int fd, bool pressed)
{
    return emit(fd, EV_ABS, ABS_MT_TRACKING_ID, pressed ? 1 : -1) &&
           emit(fd, EV_KEY, BTN_TOUCH, pressed ? 1 : 0) &&
           emit(fd, EV_SYN, SYN_REPORT, 0);
}

int integer(const char* value)
{
    return static_cast<int>(std::strtol(value, nullptr, 10));
}

}  // namespace

int main(int argc, char** argv)
{
    if(argc < 4) {
        std::cerr << "usage: evdev_inject tap x y [hold_ms] | drag x1 y1 x2 y2 duration_ms\n";
        return 64;
    }

    const auto& profile = dictpen::y01_profile();
    const int fd = open_touch(profile.touch_name);
    if(fd < 0) {
        std::cerr << "inject.error=open: " << std::strerror(errno) << '\n';
        return 2;
    }

    bool ok = false;
    const std::string operation = argv[1];
    if(operation == "tap") {
        const int hold_ms = argc > 4 ? std::max(1, integer(argv[4])) : 80;
        ok = contact(fd, true) && point(fd, integer(argv[2]), integer(argv[3]), profile);
        std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
        ok = contact(fd, false) && ok;
    }
    else if(operation == "drag" && argc >= 7) {
        const int x1 = integer(argv[2]);
        const int y1 = integer(argv[3]);
        const int x2 = integer(argv[4]);
        const int y2 = integer(argv[5]);
        const int duration_ms = std::max(50, integer(argv[6]));
        const int steps = std::max(2, duration_ms / 16);
        ok = contact(fd, true);
        for(int step = 0; step <= steps && ok; ++step) {
            const int x = x1 + (x2 - x1) * step / steps;
            const int y = y1 + (y2 - y1) * step / steps;
            ok = point(fd, x, y, profile);
            std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms / steps));
        }
        ok = contact(fd, false) && ok;
    }

    close(fd);
    if(!ok) {
        std::cerr << "inject.error=write: " << std::strerror(errno) << '\n';
        return 3;
    }
    std::cout << "inject.result=PASS operation=" << operation << '\n';
    return 0;
}
