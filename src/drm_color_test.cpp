#include "platform/device_profile/device_profile.h"
#include "platform/drm/drm_backend.h"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>

namespace {

uint32_t color_for_frame(int frame)
{
    static constexpr uint32_t colors[] {
        0x001D3557U,
        0x00E63946U,
        0x00F1FAEEU,
        0x00A8DADC,
        0x00457B9DU,
    };
    return colors[frame % static_cast<int>(std::size(colors))];
}

}  // namespace

int main(int argc, char** argv)
{
    int seconds = 5;
    if(argc > 1) seconds = std::max(1, std::atoi(argv[1]));

    const auto& profile = dictpen::y01_profile();
    dictpen::DrmBackend display(profile);
    if(!display.open()) {
        std::cerr << "color_test.error=open: " << display.last_error() << '\n';
        return 2;
    }

    std::vector<uint32_t> frame(static_cast<size_t>(profile.logical_width) * profile.logical_height);
    const int frames = seconds * 3;
    for(int index = 0; index < frames; ++index) {
        std::fill(frame.begin(), frame.end(), color_for_frame(index));
        if(!display.present_logical(frame.data(), profile.logical_width, profile.logical_height)) {
            std::cerr << "color_test.error=present: " << display.last_error() << '\n';
            display.close();
            return 3;
        }
        std::cout << "color_test.frame=" << index << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds(333));
    }
    display.close();
    std::cout << "color_test.result=PASS\n";
    return 0;
}
