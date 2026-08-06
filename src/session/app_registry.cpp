#include "session/app_registry.h"

namespace dictpen {
namespace {

constexpr AppDescriptor kApps[] {
    {AppId::launcher, "launcher", "应用", "lvgl_launcher", "\xEF\x80\x95", "选择应用"},
    {AppId::poc, "poc", "LVGL 体验", "lvgl_poc", "\xEF\x84\x9B", "交互与设备诊断"},
    {AppId::focus_timer, "focus_timer", "专注计时", "focus_timer", "\xEF\x89\x92", "快速开始一轮专注"},
};

}  // namespace

const AppDescriptor* app_registry(std::size_t& count)
{
    count = sizeof(kApps) / sizeof(kApps[0]);
    return kApps;
}

const AppDescriptor* find_app(AppId id)
{
    std::size_t count = 0;
    const AppDescriptor* apps = app_registry(count);
    for(std::size_t index = 0; index < count; ++index) {
        if(apps[index].id == id) return &apps[index];
    }
    return nullptr;
}

}  // namespace dictpen
