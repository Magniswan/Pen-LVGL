#pragma once

#include "platform/input/input_backend.h"

#include <chrono>
#include <cstdint>

namespace dictpen {

struct MetricsSnapshot {
    double fps {0.0};
    double average_frame_ms {0.0};
    double peak_frame_ms {0.0};
    double cpu_percent {0.0};
    double peak_cpu_percent {0.0};
    uint64_t frames {0};
    uint64_t input_events {0};
    long rss_kb {0};
    long peak_rss_kb {0};
    PointerState pointer;
};

class RuntimeMetrics {
public:
    RuntimeMetrics();

    void record_frame(double frame_ms);
    void record_pointer(PointerState pointer);
    MetricsSnapshot snapshot();
    double runtime_seconds() const;

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point started_;
    Clock::time_point period_started_;
    uint64_t frames_ {0};
    uint64_t period_frames_ {0};
    uint64_t input_events_ {0};
    double frame_ms_total_ {0.0};
    double peak_frame_ms_ {0.0};
    double fps_ {0.0};
    long peak_rss_kb_ {0};
    long clock_ticks_per_second_ {100};
    uint64_t last_cpu_ticks_ {0};
    double cpu_percent_ {0.0};
    double peak_cpu_percent_ {0.0};
    Clock::time_point cpu_sample_started_;
    PointerState pointer_;
};

}  // namespace dictpen
