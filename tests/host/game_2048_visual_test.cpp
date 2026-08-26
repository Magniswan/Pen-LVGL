#include "game_2048_ui.h"

#include <lvgl.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

constexpr std::int32_t kWidth = 960;
constexpr std::int32_t kHeight = 266;
constexpr std::array<std::uint16_t, 16> kGoldenBoard {
    2, 2, 4, 8,
    16, 32, 64, 128,
    256, 512, 1024, 2048,
    0, 0, 4, 4,
};
constexpr std::uint64_t kInitialGolden = 0x4f401cd6b40cd341ULL;
constexpr std::uint64_t kMotionGolden = 0xffd188ae958f41a5ULL;
constexpr std::uint64_t kResolutionGolden = 0x01edd144e4d304dcULL;
constexpr std::uint64_t kTerminalGolden = 0xfe92aa8a9bd7418eULL;
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

void set_reduced_motion(bool enabled)
{
#if defined(_WIN32)
    if(_putenv_s("LVGL_REDUCED_MOTION", enabled ? "1" : "") != 0) std::abort();
#else
    const auto result = enabled ? setenv("LVGL_REDUCED_MOTION", "1", 1)
                                : unsetenv("LVGL_REDUCED_MOTION");
    if(result != 0) std::abort();
#endif
}

void advance(lv_display_t* display, std::uint32_t milliseconds)
{
    for(std::uint32_t elapsed = 0; elapsed < milliseconds; elapsed += 16) {
        g_tick += 16;
        lv_timer_handler();
        lv_refr_now(display);
    }
}

std::uint64_t frame_digest(const std::vector<std::uint32_t>& frame)
{
    std::uint64_t digest = 14695981039346656037ULL;
    for(const auto pixel : frame) {
        for(unsigned shift = 0; shift < 24; shift += 8) {
            digest ^= (pixel >> shift) & 0xffU;
            digest *= 1099511628211ULL;
        }
    }
    return digest;
}

bool write_ppm(const std::filesystem::path& path, const std::vector<std::uint32_t>& frame)
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

struct Fixture {
    std::vector<std::uint32_t> frame = std::vector<std::uint32_t>(
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight));
    std::vector<std::uint32_t> buffer = std::vector<std::uint32_t>(
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight));
    lv_display_t* display {nullptr};

    Fixture()
    {
        g_tick = 0;
        lv_init();
        lv_tick_set_cb(tick);
        display = lv_display_create(kWidth, kHeight);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
        lv_display_set_user_data(display, &frame);
        lv_display_set_flush_cb(display, flush);
        lv_display_set_buffers(
            display, buffer.data(), nullptr,
            static_cast<std::uint32_t>(buffer.size() * sizeof(std::uint32_t)),
            LV_DISPLAY_RENDER_MODE_FULL);
    }

    ~Fixture() { lv_deinit(); }
};

bool capture(
    const std::filesystem::path& output_directory, std::string_view name,
    const std::vector<std::uint32_t>& frame, std::uint64_t expected)
{
    const auto digest = frame_digest(frame);
    std::cout << name << "=0x" << std::hex << std::setw(16) << std::setfill('0') << digest
              << std::dec << '\n';
    const auto path = output_directory / (std::string(name) + ".ppm");
    if(!write_ppm(path, frame)) {
        std::cerr << "could not write visual evidence: " << path << '\n';
        return false;
    }
    if(digest != expected) {
        std::cerr << "golden mismatch for " << name << ": expected 0x" << std::hex
                  << expected << ", observed 0x" << digest << std::dec << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    if(argc != 2) return 2;
    const auto output_directory = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(output_directory);

    bool passed = true;
    std::uint64_t terminal_digest = 0;
    set_reduced_motion(false);
    {
        Fixture fixture;
        lvgl_apps::Game2048Ui game(0x2048U, nullptr);
        game.visual_test_set_board(kGoldenBoard, 4096);
        game.create();
        lv_obj_update_layout(lv_screen_active());
        if(!game.visual_test_geometry_valid()) {
            std::cerr << "one or more 2048 cells exceed the board boundary\n";
            passed = false;
        }
        advance(fixture.display, 416);
        passed &= capture(output_directory, "game-2048-initial", fixture.frame, kInitialGolden);

        game.visual_test_request_move(lvgl_apps::MoveDirection::left);
        advance(fixture.display, 80);
        passed &= capture(output_directory, "game-2048-motion", fixture.frame, kMotionGolden);
        advance(fixture.display, 128);
        passed &= capture(
            output_directory, "game-2048-resolution", fixture.frame, kResolutionGolden);
        advance(fixture.display, 320);
        terminal_digest = frame_digest(fixture.frame);
        passed &= capture(output_directory, "game-2048-terminal", fixture.frame, kTerminalGolden);
        game.destroy();
    }

    set_reduced_motion(true);
    {
        Fixture fixture;
        lvgl_apps::Game2048Ui game(0x2048U, nullptr);
        game.visual_test_set_board(kGoldenBoard, 4096);
        game.create();
        game.visual_test_request_move(lvgl_apps::MoveDirection::left);
        advance(fixture.display, 16);
        const auto reduced_digest = frame_digest(fixture.frame);
        passed &= capture(
            output_directory, "game-2048-reduced-motion", fixture.frame, kTerminalGolden);
        if(reduced_digest != terminal_digest) {
            std::cerr << "reduced-motion terminal frame differs from animated terminal frame\n";
            passed = false;
        }
        game.destroy();
    }
    set_reduced_motion(false);

    if(!passed) return 1;
    std::cout << "VISUAL_PASS deterministic keyframes and reduced-motion parity\n";
    return 0;
}
