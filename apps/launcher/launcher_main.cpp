#include "launcher/launcher_ui.h"
#include "runtime/platform_runtime.h"
#include "shell/app_shell.h"

#include <memory>

namespace {

class LauncherApplication final : public dictpen::RuntimeApplication {
public:
    void create(dictpen::RuntimeContext& context) override
    {
        ui_ = std::make_unique<dictpen::LauncherUi>(context.control);
        ui_->create();
        shell_ = std::make_unique<dictpen::AppShell>(
            context.control, dictpen::AppShellConfig {"应用中心", true});
        shell_->create();
    }

    bool stop_requested() const override
    {
        return (ui_ && ui_->stop_requested()) || (shell_ && shell_->stop_requested());
    }

    void destroy() override
    {
        if(shell_) shell_->destroy();
    }

private:
    std::unique_ptr<dictpen::LauncherUi> ui_;
    std::unique_ptr<dictpen::AppShell> shell_;
};

}  // namespace

int main()
{
    LauncherApplication application;
    dictpen::PlatformRuntimeOptions options;
    options.app_id = "launcher";
    return dictpen::run_platform_application(application, options);
}
