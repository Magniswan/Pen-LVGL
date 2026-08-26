#pragma once

#include "lvgl_platform/app_control.hpp"
#include "lvgl_platform/app_storage.hpp"

#include <cstdint>

namespace dictpen {

inline constexpr std::uint32_t sdk_abi_major = 1;
inline constexpr std::uint32_t sdk_abi_minor = 0;
inline constexpr const char* sdk_abi = "1.0";

struct AppContext {
    AppControl& control;
    AppStorage& storage;
};

using RuntimeContext = AppContext;

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
