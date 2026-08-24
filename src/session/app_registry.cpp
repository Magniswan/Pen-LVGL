#include "session/app_registry.h"

namespace dictpen {
namespace {

constexpr AppDescriptor kApps[] {
    {AppId::launcher, "top.lvgl.desktop", "应用", "lvgl-desktop", "\xEF\x80\x95", "选择应用"},
    {AppId::game_2048, "top.lvgl.game2048", "2048", "lvgl-2048", "\xEF\x84\x9B", "融合矿石，建立最高分"},
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
