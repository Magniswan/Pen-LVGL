#include "platform/device_profile/device_profile.h"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <csignal>
#include <thread>

namespace {

using namespace std::chrono_literals;

std::atomic_bool g_start {false};

void start_handler(int)
{
    g_start.store(true);
}

bool emit(int fd, uint16_t type, uint16_t code, int32_t value)
{
    input_event event {};
    event.type = type;
    event.code = code;
    event.value = value;
    return write(fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event));
}

bool sync(int fd)
{
    return emit(fd, EV_SYN, SYN_REPORT, 0);
}

bool point(int fd, int32_t logical_x, int32_t logical_y, const dictpen::DeviceProfile& profile)
{
    const int32_t raw_x = std::clamp(logical_y + profile.touch_cross_axis_offset,
                                     int32_t {0}, profile.physical_width);
    const int32_t raw_y = std::clamp(profile.physical_height - 1 - logical_x,
                                     int32_t {0}, profile.physical_height);
    return emit(fd, EV_ABS, ABS_MT_POSITION_X, raw_x) &&
           emit(fd, EV_ABS, ABS_MT_POSITION_Y, raw_y) && sync(fd);
}

bool contact(int fd, bool pressed)
{
    return emit(fd, EV_ABS, ABS_MT_TRACKING_ID, pressed ? 1 : -1) &&
           emit(fd, EV_KEY, BTN_TOUCH, pressed ? 1 : 0) && sync(fd);
}

bool tap(int fd, int x, int y, int hold_ms, const dictpen::DeviceProfile& profile)
{
    bool ok = point(fd, x, y, profile) && contact(fd, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
    ok = contact(fd, false) && ok;
    std::this_thread::sleep_for(250ms);
    return ok;
}

bool drag(int fd, int x1, int y1, int x2, int y2, int duration_ms,
          const dictpen::DeviceProfile& profile)
{
    bool ok = point(fd, x1, y1, profile) && contact(fd, true);
    const int steps = std::max(2, duration_ms / 16);
    for(int step = 1; step <= steps && ok; ++step) {
        ok = point(fd, x1 + (x2 - x1) * step / steps,
                   y1 + (y2 - y1) * step / steps, profile);
        std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms / steps));
    }
    ok = contact(fd, false) && ok;
    std::this_thread::sleep_for(250ms);
    return ok;
}

int create_device(const dictpen::DeviceProfile& profile)
{
    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if(fd < 0) return -1;
    if(ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0 ||
       ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
       ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID) < 0 ||
       ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X) < 0 ||
       ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y) < 0 ||
       ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH) < 0) {
        close(fd);
        return -1;
    }

    uinput_user_dev device {};
    std::strncpy(device.name, "poc_touch", UINPUT_MAX_NAME_SIZE - 1);
    device.id.bustype = BUS_VIRTUAL;
    device.id.vendor = 0x1209;
    device.id.product = 0x0001;
    device.id.version = 1;
    device.absmin[ABS_MT_TRACKING_ID] = 0;
    device.absmax[ABS_MT_TRACKING_ID] = 65535;
    device.absmin[ABS_MT_POSITION_X] = 0;
    device.absmax[ABS_MT_POSITION_X] = profile.physical_width;
    device.absmin[ABS_MT_POSITION_Y] = 0;
    device.absmax[ABS_MT_POSITION_Y] = profile.physical_height;
    if(write(fd, &device, sizeof(device)) != static_cast<ssize_t>(sizeof(device)) ||
       ioctl(fd, UI_DEV_CREATE) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

int main(int argc, char** argv)
{
    const bool wait_for_signal = argc > 1 && std::strcmp(argv[1], "wait") == 0;
    const int delay_ms = wait_for_signal ? 0 : (argc > 1 ? std::max(0, std::atoi(argv[1])) : 20000);
    const bool visual_only = argc > 2 && std::strcmp(argv[2], "visual") == 0;
    if(wait_for_signal) std::signal(SIGUSR1, start_handler);
    const auto& profile = dictpen::y01_profile();
    const int fd = create_device(profile);
    if(fd < 0) {
        std::cerr << "uinput.error=create: " << std::strerror(errno) << '\n';
        return 2;
    }
    std::cout << "uinput.created name=poc_touch delay_ms=" << delay_ms << '\n';
    std::cout.flush();
    if(wait_for_signal) {
        for(int elapsed = 0; elapsed < 120000 && !g_start.load(); elapsed += 50) {
            std::this_thread::sleep_for(50ms);
        }
        if(!g_start.load()) {
            ioctl(fd, UI_DEV_DESTROY);
            close(fd);
            std::cerr << "uinput.error=signal_timeout\n";
            return 4;
        }
    }
    else {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }

    bool ok = tap(fd, 170, 27, 100, profile);       // Visual page.
    if(visual_only) {
        std::this_thread::sleep_for(5000ms);
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
        std::cout << "uinput.result=" << (ok ? "PASS" : "FAIL") << " mode=visual\n";
        return ok ? 0 : 3;
    }
    std::this_thread::sleep_for(1500ms);
    ok = tap(fd, 278, 27, 100, profile) && ok;      // Diagnostics page.
    ok = tap(fd, 58, 27, 100, profile) && ok;       // Interaction page.
    ok = tap(fd, 80, 120, 100, profile) && ok;      // Rapid tap button.
    ok = tap(fd, 80, 120, 900, profile) && ok;      // Long press.
    ok = drag(fd, 140, 162, 250, 162, 500, profile) && ok;
    ok = drag(fd, 700, 140, 820, 170, 600, profile) && ok;

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    std::cout << "uinput.result=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 3;
}
