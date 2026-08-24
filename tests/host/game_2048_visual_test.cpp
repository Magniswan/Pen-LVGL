#include "game_2048_ui.h"

#include <lvgl.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>

namespace {

constexpr std::int32_t kWidth = 960;
constexpr std::int32_t kHeight = 266;
std::uint32_t g_tick = 0;

std::uint32_t tick() { return g_tick; }

void flush(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixels)
{
    auto* frame = static_cast<std::vector<std::uint32_t>*>(lv_display_get_user_data(display));
    const auto* source = reinterpret_cast<const std::uint32_t*>(pixels);
    const auto width = lv_area_get_width(area);
    for(std::int32_t row = area->y1; row <= area->y2; ++row) {
        const auto destination = static_cast<std::size_t>(row) * kWidth + area->x1;
        const auto source_row = static_cast<std::size_t>(row - area->y1) * width;
        for(std::int32_t column = 0; column < width; ++column) {
            (*frame)[destination + column] = source[source_row + column];
        }
    }
    lv_display_flush_ready(display);
}

bool write_ppm(const char* path, const std::vector<std::uint32_t>& frame)
{
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
    for(const auto pixel : frame) {
        const char rgb[] {
            static_cast<char>((pixel >> 16U) & 0xffU),
            static_cast<char>((pixel >> 8U) & 0xffU),
            static_cast<char>(pixel & 0xffU),
        };
        output.write(rgb, sizeof(rgb));
    }
    return output.good();
}

}  // namespace

int main(int argc, char** argv)
{
    if(argc != 2) return 2;
    lv_init();
    lv_tick_set_cb(tick);
    std::vector<std::uint32_t> frame(static_cast<std::size_t>(kWidth) * kHeight);
    std::vector<std::uint32_t> buffer(frame.size());
    auto* display = lv_display_create(kWidth, kHeight);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_user_data(display, &frame);
    lv_display_set_flush_cb(display, flush);
    lv_display_set_buffers(
        display, buffer.data(), nullptr,
        static_cast<std::uint32_t>(buffer.size() * sizeof(std::uint32_t)),
        LV_DISPLAY_RENDER_MODE_FULL);

    lvgl_apps::Game2048Ui game(0x2048U, nullptr);
    game.create();
    for(int frame_index = 0; frame_index < 30; ++frame_index) {
        g_tick += 16;
        lv_timer_handler();
        lv_refr_now(display);
    }

    std::set<std::uint32_t> colors(frame.begin(), frame.end());
    const bool visual_content = colors.size() >= 12 && frame[0] != frame[133 * kWidth + 480];
    const bool written = visual_content && write_ppm(argv[1], frame);
    game.destroy();
    lv_deinit();
    if(!written) {
        std::cerr << "visual smoke frame is empty or could not be written\n";
        return 1;
    }
    std::cout << "VISUAL_PASS colors=" << colors.size() << " output=" << argv[1] << '\n';
    return 0;
}
