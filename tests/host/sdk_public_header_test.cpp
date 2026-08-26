#include <lvgl_platform/sdk.hpp>

#include <type_traits>

namespace {

class HeaderProbeApplication final : public dictpen::RuntimeApplication {
public:
    void create(dictpen::RuntimeContext&) override {}
    bool stop_requested() const override { return true; }
};

static_assert(dictpen::sdk_abi_major == 1);
static_assert(dictpen::sdk_abi_minor == 0);
static_assert(std::is_same_v<dictpen::AppContext, dictpen::RuntimeContext>);
static_assert(std::has_virtual_destructor_v<dictpen::RuntimeApplication>);
static_assert(!std::is_copy_constructible_v<dictpen::AppStorage>);
static_assert(!std::is_copy_constructible_v<dictpen::AppShell>);

}  // namespace

int main()
{
    HeaderProbeApplication application;
    return application.stop_requested() ? 0 : 1;
}
