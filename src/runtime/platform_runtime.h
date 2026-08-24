#pragma once

#include "diagnostics/runtime_metrics.h"
#include "runtime/app_control.h"
#include "runtime/app_storage.h"

namespace dictpen {

struct RuntimeContext {
    RuntimeMetrics& metrics;
    AppControl& control;
    AppStorage& storage;
};

class RuntimeApplication {
public:
    virtual ~RuntimeApplication() = default;

    virtual void create(RuntimeContext& context) = 0;
    virtual bool stop_requested() const = 0;
    virtual void destroy() {}
};

struct PlatformRuntimeOptions {
    const char* app_id {"app"};
    const char* run_seconds_env {"LVGL_RUN_SECONDS"};
    int default_run_seconds {0};
    const char* fail_after_env {nullptr};
    bool log_metrics {false};
};

int run_platform_application(RuntimeApplication& application,
                             const PlatformRuntimeOptions& options);

}  // namespace dictpen
