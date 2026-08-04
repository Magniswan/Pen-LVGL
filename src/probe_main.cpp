#include "platform/device_profile/device_profile.h"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>

namespace {

struct InputDevice {
    std::string path;
    int fd {-1};

    InputDevice() = default;
    InputDevice(const InputDevice&) = delete;
    InputDevice& operator=(const InputDevice&) = delete;

    InputDevice(InputDevice&& other) noexcept
        : path(std::move(other.path)), fd(other.fd)
    {
        other.fd = -1;
    }

    ~InputDevice()
    {
        if(fd >= 0) close(fd);
    }
};

std::optional<InputDevice> find_input_device(const char* expected_name)
{
    for(int index = 0; index < 32; ++index) {
        InputDevice device;
        device.path = "/dev/input/event" + std::to_string(index);
        device.fd = open(device.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if(device.fd < 0) continue;

        char name[128] {};
        if(ioctl(device.fd, EVIOCGNAME(sizeof(name)), name) >= 0 &&
           std::strcmp(name, expected_name) == 0) {
            return device;
        }
        close(device.fd);
        device.fd = -1;
    }
    return std::nullopt;
}

void print_axis(int fd, unsigned int code, const char* label)
{
    input_absinfo info {};
    if(ioctl(fd, EVIOCGABS(code), &info) == 0) {
        std::cout << "axis." << label << "=" << info.minimum << ".." << info.maximum
                  << " fuzz=" << info.fuzz << " flat=" << info.flat << '\n';
    }
}

bool run_coordinate_self_test()
{
    using dictpen::Point;
    const auto& profile = dictpen::y01_profile();

    struct Case {
        const char* name;
        Point actual;
        Point expected;
    };

    const Case cases[] {
        {"display.top_left", dictpen::logical_to_physical({0, 0}, profile), {107, 959}},
        {"display.bottom_right", dictpen::logical_to_physical({959, 265}, profile), {372, 0}},
        {"touch.top_left", dictpen::raw_touch_to_logical({113, 959}, profile), {0, 0}},
        {"touch.bottom_right", dictpen::raw_touch_to_logical({378, 0}, profile), {959, 265}},
        {"touch.clamp_low", dictpen::raw_touch_to_logical({0, 1200}, profile), {0, 0}},
        {"touch.clamp_high", dictpen::raw_touch_to_logical({480, -10}, profile), {959, 265}},
    };

    bool passed = true;
    for(const auto& test : cases) {
        const bool ok = test.actual == test.expected;
        std::cout << "self_test." << test.name << '=' << (ok ? "PASS" : "FAIL")
                  << " actual=" << test.actual.x << ',' << test.actual.y
                  << " expected=" << test.expected.x << ',' << test.expected.y << '\n';
        passed = passed && ok;
    }
    return passed;
}

}  // namespace

int main(int argc, char** argv)
{
    const bool self_test = argc > 1 && std::string(argv[1]) == "--self-test";
    const auto& profile = dictpen::y01_profile();

    utsname system {};
    if(uname(&system) != 0) {
        std::cerr << "probe.error=uname: " << std::strerror(errno) << '\n';
        return 2;
    }

    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::cout << "probe.version=0.1.0\n"
              << "profile.id=" << profile.id << '\n'
              << "profile.sku=" << profile.sku << '\n'
              << "system.machine=" << system.machine << '\n'
              << "system.release=" << system.release << '\n'
              << "clock.monotonic_ms=" << uptime_ms << '\n'
              << "display.logical=" << profile.logical_width << 'x' << profile.logical_height << '\n'
              << "display.physical=" << profile.physical_width << 'x' << profile.physical_height << '\n';

    auto input = find_input_device(profile.touch_name);
    if(!input) {
        std::cerr << "input.error=device_not_found name=" << profile.touch_name << '\n';
        return 3;
    }

    std::cout << "input.path=" << input->path << '\n';
    print_axis(input->fd, ABS_X, "x");
    print_axis(input->fd, ABS_Y, "y");
    print_axis(input->fd, ABS_MT_POSITION_X, "mt_x");
    print_axis(input->fd, ABS_MT_POSITION_Y, "mt_y");

    if(self_test && !run_coordinate_self_test()) return 4;

    std::cout << "probe.result=PASS\n";
    return 0;
}
