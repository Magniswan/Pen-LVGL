#include "game_2048_ui.h"

#include "runtime/platform_runtime.h"
#include "shell/app_shell.h"

#include <chrono>
#include <memory>

namespace {

class GameApplication final : public dictpen::RuntimeApplication {
public:
    void create(dictpen::RuntimeContext& context) override
    {
        const auto seed = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        ui_ = std::make_unique<lvgl_apps::Game2048Ui>(seed, &context.storage);
        ui_->create();
        shell_ = std::make_unique<dictpen::AppShell>(
            context.control, dictpen::AppShellConfig {"2048", false});
        shell_->create();
    }

    bool stop_requested() const override
    {
        return (ui_ && ui_->stop_requested()) || (shell_ && shell_->stop_requested());
    }

    void destroy() override
    {
        if(ui_) ui_->destroy();
        if(shell_) shell_->destroy();
    }

private:
    std::unique_ptr<lvgl_apps::Game2048Ui> ui_;
    std::unique_ptr<dictpen::AppShell> shell_;
};

}  // namespace

int main()
{
    GameApplication application;
    dictpen::PlatformRuntimeOptions options;
    options.app_id = "top.lvgl.game2048";
    return dictpen::run_platform_application(application, options);
}
