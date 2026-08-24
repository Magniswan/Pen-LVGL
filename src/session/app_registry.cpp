#include "session/app_registry.h"

#include "lvgl_platform/application_registry.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dictpen {
namespace {

constexpr AppDescriptor kDevelopmentApps[] {
    {AppId::launcher, "top.lvgl.desktop", "应用", "lvgl-desktop", "\xEF\x80\x95", "选择应用"},
    {AppId::game_2048, "top.lvgl.game2048", "2048", "lvgl-2048", "\xEF\x84\x9B", "融合矿石，建立最高分"},
};

struct RegistryStorage {
    std::vector<lvgl_platform::RegisteredApplication> owned;
    std::vector<AppDescriptor> descriptors;
};

#if !defined(_WIN32)
int registry_descriptor() noexcept
{
    const char* value = std::getenv("LVGL_APP_REGISTRY_FD");
    if(value == nullptr || *value == '\0') return -1;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if(errno != 0 || end == value || *end != '\0' || parsed < 0 ||
       parsed > std::numeric_limits<int>::max()) {
        return -1;
    }
    return static_cast<int>(parsed);
}

RegistryStorage load_registry()
{
    RegistryStorage storage;
    const int descriptor = registry_descriptor();
    struct stat details {};
    if(descriptor < 0 || ::fstat(descriptor, &details) != 0 || !S_ISREG(details.st_mode) ||
       details.st_uid != 0 || details.st_nlink != 1 || (details.st_mode & 0222) != 0 ||
       details.st_size <= 0 ||
       static_cast<std::uint64_t>(details.st_size) > lvgl_platform::kMaximumRegistrySize) {
        return storage;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(details.st_size));
    std::size_t received = 0;
    while(received < bytes.size()) {
        const auto count = ::pread(
            descriptor, bytes.data() + received, bytes.size() - received,
            static_cast<off_t>(received));
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return {};
        received += static_cast<std::size_t>(count);
    }
    auto parsed = lvgl_platform::decode_application_registry(bytes.data(), bytes.size());
    if(!parsed.ok()) return storage;
    storage.owned = std::move(parsed.applications);
    storage.descriptors.reserve(storage.owned.size() + 1);
    storage.descriptors.push_back(kDevelopmentApps[0]);
    for(const auto& application : storage.owned) {
        storage.descriptors.push_back({
            AppId::none, application.app_id.c_str(), application.name.c_str(), "", "\xEF\x82\x85",
            application.version.c_str()});
    }
    return storage;
}
#endif

RegistryStorage& runtime_registry()
{
#if !defined(_WIN32)
    static RegistryStorage storage = load_registry();
#else
    static RegistryStorage storage;
#endif
    return storage;
}

}  // namespace

const AppDescriptor* app_registry(std::size_t& count)
{
    auto& runtime = runtime_registry();
    if(!runtime.descriptors.empty()) {
        count = runtime.descriptors.size();
        return runtime.descriptors.data();
    }
#if !defined(_WIN32)
    // A target desktop without the session-owned registry descriptor exposes no
    // launchable application. This is a fail-closed state, not a development fallback.
    count = 1;
    return kDevelopmentApps;
#else
    count = sizeof(kDevelopmentApps) / sizeof(kDevelopmentApps[0]);
    return kDevelopmentApps;
#endif
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

const AppDescriptor* find_app(const char* stable_id)
{
    if(stable_id == nullptr) return nullptr;
    std::size_t count = 0;
    const AppDescriptor* apps = app_registry(count);
    for(std::size_t index = 0; index < count; ++index) {
        if(std::string_view(apps[index].stable_id) == stable_id) return &apps[index];
    }
    return nullptr;
}

}  // namespace dictpen
