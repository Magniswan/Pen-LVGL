#include "runtime/platform_runtime.h"

#include "platform/device_profile/device_profile.h"
#include "platform/drm/drm_backend.h"
#include "platform/input/input_backend.h"

#include <lvgl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace dictpen {
namespace {

using Clock = std::chrono::steady_clock;

struct DisplayContext {
    DrmBackend* backend {nullptr};
    RuntimeMetrics* metrics {nullptr};
    std::atomic_bool failed {false};
    const char* capture_path {nullptr};
    int capture_frame {0};
    int frames {0};
    bool captured {false};
    std::vector<uint32_t>* logical_frame {nullptr};
    int32_t logical_width {0};
    int32_t logical_height {0};
};

struct InputContext {
    InputBackend* backend {nullptr};
    RuntimeMetrics* metrics {nullptr};
};

std::atomic_bool g_stop {false};
Clock::time_point g_tick_start;

void signal_handler(int)
{
    g_stop.store(true);
}

uint32_t tick_ms()
{
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_tick_start).count());
}

int env_integer(const char* name, int fallback)
{
    if(!name) return fallback;
    const char* value = std::getenv(name);
    if(!value || *value == '\0') return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if(end == value || *end != '\0') return fallback;
    return static_cast<int>(parsed);
}

void capture_frame(DisplayContext& context, lv_display_t* display)
{
    if(context.captured || !context.capture_path || context.capture_frame <= 0 ||
       context.frames < context.capture_frame) {
        return;
    }

    const int32_t width = static_cast<int32_t>(lv_display_get_horizontal_resolution(display));
    const int32_t height = static_cast<int32_t>(lv_display_get_vertical_resolution(display));
    std::ofstream capture(context.capture_path, std::ios::binary);
    capture << "P6\n" << width << ' ' << height << "\n255\n";
    for(int32_t index = 0; index < width * height; ++index) {
        const uint32_t value = context.logical_frame->at(static_cast<std::size_t>(index));
        const char rgb[] {
            static_cast<char>((value >> 16U) & 0xFFU),
            static_cast<char>((value >> 8U) & 0xFFU),
            static_cast<char>(value & 0xFFU),
        };
        capture.write(rgb, sizeof(rgb));
    }
    context.captured = capture.good();
    std::cout << "CAPTURE path=" << context.capture_path
              << " result=" << (context.captured ? "PASS" : "FAIL") << '\n';
}

void display_flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels)
{
    auto* context = static_cast<DisplayContext*>(lv_display_get_user_data(display));
    const int32_t area_width = lv_area_get_width(area);
    const int32_t area_height = lv_area_get_height(area);
    const auto* source = reinterpret_cast<const uint32_t*>(pixels);
    for(int32_t row = 0; row < area_height; ++row) {
        const std::size_t destination_offset =
            static_cast<std::size_t>(area->y1 + row) * context->logical_width + area->x1;
        std::memcpy(context->logical_frame->data() + destination_offset,
                    source + static_cast<std::size_t>(row) * area_width,
                    static_cast<std::size_t>(area_width) * sizeof(uint32_t));
    }

    if(!lv_display_flush_is_last(display)) {
        lv_display_flush_ready(display);
        return;
    }

    const auto started = Clock::now();
    const bool ok = context->backend->present_logical(
        context->logical_frame->data(), context->logical_width, context->logical_height);
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    context->metrics->record_frame(elapsed_ms);
    ++context->frames;
    capture_frame(*context, display);
    if(!ok) {
        std::cerr << "RUNTIME error=present detail=\"" << context->backend->last_error() << "\"\n";
        context->failed.store(true);
    }
    lv_display_flush_ready(display);
}

void input_read(lv_indev_t* input_device, lv_indev_data_t* data)
{
    auto* context = static_cast<InputContext*>(lv_indev_get_user_data(input_device));
    context->backend->poll();
    const PointerState state = context->backend->state();
    context->metrics->record_pointer(state);
    data->point.x = state.point.x;
    data->point.y = state.point.y;
    data->state = state.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

}  // namespace

int run_platform_application(RuntimeApplication& application,
                             const PlatformRuntimeOptions& options)
{
    g_stop.store(false);
    g_tick_start = Clock::now();
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGHUP, signal_handler);

    const int run_seconds = std::max(0, env_integer(options.run_seconds_env,
                                                    options.default_run_seconds));
    const int fail_after = std::max(0, env_integer(options.fail_after_env, 0));
    DeviceProfile profile = y01_profile();
    if(const char* touch_name = std::getenv("POC_TOUCH_NAME")) profile.touch_name = touch_name;
    DrmBackend drm(profile);
    InputBackend input(profile);
    RuntimeMetrics metrics;
    AppControl control;

    if(!drm.open()) {
        std::cerr << "RUNTIME error=drm_open detail=\"" << drm.last_error() << "\"\n";
        return 2;
    }
    if(!input.open()) {
        std::cerr << "RUNTIME error=input_open detail=\"" << input.last_error() << "\"\n";
        return 3;
    }

    lv_init();
    lv_tick_set_cb(tick_ms);

    constexpr int32_t kRenderRows = 48;
    const std::size_t pixel_count =
        static_cast<std::size_t>(profile.logical_width) * profile.logical_height;
    const std::size_t render_pixel_count =
        static_cast<std::size_t>(profile.logical_width) * kRenderRows;
    std::vector<uint32_t> logical_frame(pixel_count);
    std::vector<uint32_t> frame_a(render_pixel_count);
    std::vector<uint32_t> frame_b(render_pixel_count);
    DisplayContext display_context {&drm, &metrics};
    display_context.capture_path = std::getenv("POC_CAPTURE_PATH");
    display_context.capture_frame = env_integer("POC_CAPTURE_FRAME", 0);
    display_context.logical_frame = &logical_frame;
    display_context.logical_width = profile.logical_width;
    display_context.logical_height = profile.logical_height;
    InputContext input_context {&input, &metrics};

    lv_display_t* display = lv_display_create(profile.logical_width, profile.logical_height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_user_data(display, &display_context);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(display, frame_a.data(), frame_b.data(),
                           render_pixel_count * sizeof(uint32_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* input_device = lv_indev_create();
    lv_indev_set_type(input_device, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input_device, display);
    lv_indev_set_user_data(input_device, &input_context);
    lv_indev_set_read_cb(input_device, input_read);

    RuntimeContext runtime_context {metrics, control};
    application.create(runtime_context);

    std::cout << "RUNTIME start app=" << options.app_id << " profile=" << profile.id
              << " logical=" << profile.logical_width << 'x' << profile.logical_height
              << " run_seconds=" << run_seconds << " fail_after=" << fail_after << '\n';
    std::cout.flush();

    int result = 0;
    int last_metric_second = -1;
    while(!g_stop.load() && !application.stop_requested() && !display_context.failed.load()) {
        lv_timer_handler();
        const int elapsed = static_cast<int>(metrics.runtime_seconds());
        if(options.log_metrics && elapsed != last_metric_second && elapsed > 0) {
            last_metric_second = elapsed;
            const MetricsSnapshot current = metrics.snapshot();
            std::cout << "METRIC elapsed_s=" << elapsed << " fps=" << current.fps
                      << " frame_ms=" << current.average_frame_ms
                      << " peak_frame_ms=" << current.peak_frame_ms
                      << " cpu_percent=" << current.cpu_percent << " rss_kb=" << current.rss_kb
                      << " frames=" << current.frames
                      << " input_events=" << current.input_events << '\n';
            std::cout.flush();
        }
        if(fail_after > 0 && elapsed >= fail_after) {
            std::cerr << "RUNTIME injected_failure app=" << options.app_id
                      << " elapsed_s=" << elapsed << '\n';
            result = 42;
            break;
        }
        if(run_seconds > 0 && elapsed >= run_seconds) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if(display_context.failed.load()) result = 4;
    const MetricsSnapshot final = metrics.snapshot();
    std::cout << "SUMMARY app=" << options.app_id << " runtime_s=" << metrics.runtime_seconds()
              << " fps=" << final.fps << " frame_ms=" << final.average_frame_ms
              << " peak_frame_ms=" << final.peak_frame_ms
              << " cpu_percent=" << final.cpu_percent
              << " cpu_peak_percent=" << final.peak_cpu_percent
              << " rss_peak_kb=" << final.peak_rss_kb << " frames=" << final.frames
              << " input_events=" << final.input_events << " result=" << result << '\n';
    std::cout.flush();

    application.destroy();
    lv_deinit();
    input.close();
    drm.close();
    return result;
}

}  // namespace dictpen
