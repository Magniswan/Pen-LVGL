#include "diagnostics/runtime_metrics.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace dictpen {
namespace {

long resident_kb()
{
    std::ifstream statm("/proc/self/statm");
    long pages = 0;
    long resident = 0;
    if(!(statm >> pages >> resident)) return 0;
    return resident * sysconf(_SC_PAGESIZE) / 1024;
}

uint64_t process_cpu_ticks()
{
    std::ifstream stat("/proc/self/stat");
    std::string line;
    if(!std::getline(stat, line)) return 0;
    const size_t close = line.rfind(')');
    if(close == std::string::npos || close + 2 >= line.size()) return 0;
    std::istringstream fields(line.substr(close + 2));
    std::string ignored;
    for(int field = 3; field < 14; ++field) {
        if(!(fields >> ignored)) return 0;
    }
    uint64_t user = 0;
    uint64_t system = 0;
    if(!(fields >> user >> system)) return 0;
    return user + system;
}

}  // namespace

RuntimeMetrics::RuntimeMetrics()
    : started_(Clock::now()), period_started_(started_), cpu_sample_started_(started_)
{
    const long configured_ticks = sysconf(_SC_CLK_TCK);
    if(configured_ticks > 0) clock_ticks_per_second_ = configured_ticks;
    last_cpu_ticks_ = process_cpu_ticks();
}

void RuntimeMetrics::record_frame(double frame_ms)
{
    ++frames_;
    ++period_frames_;
    frame_ms_total_ += frame_ms;
    peak_frame_ms_ = std::max(peak_frame_ms_, frame_ms);
}

void RuntimeMetrics::record_pointer(PointerState pointer)
{
    if(pointer.changed) ++input_events_;
    pointer_ = pointer;
}

MetricsSnapshot RuntimeMetrics::snapshot()
{
    const auto now = Clock::now();
    const double period = std::chrono::duration<double>(now - period_started_).count();
    if(period >= 0.5) {
        fps_ = static_cast<double>(period_frames_) / period;
        period_frames_ = 0;
        period_started_ = now;
    }


    const double cpu_period = std::chrono::duration<double>(now - cpu_sample_started_).count();
    if(cpu_period >= 0.5) {
        const uint64_t ticks = process_cpu_ticks();
        const uint64_t delta = ticks >= last_cpu_ticks_ ? ticks - last_cpu_ticks_ : 0;
        cpu_percent_ = 100.0 * static_cast<double>(delta) /
                       (static_cast<double>(clock_ticks_per_second_) * cpu_period);
        peak_cpu_percent_ = std::max(peak_cpu_percent_, cpu_percent_);
        last_cpu_ticks_ = ticks;
        cpu_sample_started_ = now;
    }

    const long rss = resident_kb();
    peak_rss_kb_ = std::max(peak_rss_kb_, rss);
    return {
        fps_,
        frames_ == 0 ? 0.0 : frame_ms_total_ / static_cast<double>(frames_),
        peak_frame_ms_,
        cpu_percent_,
        peak_cpu_percent_,
        frames_,
        input_events_,
        rss,
        peak_rss_kb_,
        pointer_,
    };
}

double RuntimeMetrics::runtime_seconds() const
{
    return std::chrono::duration<double>(Clock::now() - started_).count();
}

}  // namespace dictpen
