#include "installer/installer_ui.h"
#include "runtime/platform_runtime.h"
#include "shell/app_shell.h"

#include <memory>

namespace {

class InstallerApplication final : public dictpen::RuntimeApplication {
public:
    void create(dictpen::RuntimeContext& context) override
    {
        ui_ = std::make_unique<dictpen::InstallerUi>(context.control);
        ui_->create();
        shell_ = std::make_unique<dictpen::AppShell>(
            context.control, dictpen::AppShellConfig {"应用安装器", false});
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
    std::unique_ptr<dictpen::InstallerUi> ui_;
    std::unique_ptr<dictpen::AppShell> shell_;
};

}  // namespace

int main()
{
    InstallerApplication application;
    dictpen::PlatformRuntimeOptions options;
    options.app_id = "installer";
    return dictpen::run_platform_application(application, options);
}
