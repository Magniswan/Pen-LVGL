#pragma once

#include <cstddef>
#include <cstdint>

namespace dictpen {

enum class AppId : uint32_t {
    none = 0,
    launcher = 1,
    game_2048 = 2,
};

struct AppDescriptor {
    AppId id;
    const char* stable_id;
    const char* display_name;
    const char* executable;
    const char* symbol;
    const char* summary;
};

const AppDescriptor* app_registry(std::size_t& count);
const AppDescriptor* find_app(AppId id);
const AppDescriptor* find_app(const char* stable_id);

}  // namespace dictpen
