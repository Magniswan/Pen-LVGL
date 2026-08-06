#include "focus_timer/focus_timer_ui.h"
#include "runtime/platform_runtime.h"
#include "shell/app_shell.h"

#include <memory>

namespace {

class FocusTimerApplication final : public dictpen::RuntimeApplication {
public:
    void create(dictpen::RuntimeContext& context) override
    {
        ui_ = std::make_unique<dictpen::FocusTimerUi>();
        ui_->create();
        shell_ = std::make_unique<dictpen::AppShell>(
            context.control, dictpen::AppShellConfig {"专注计时", false});
        shell_->create();
    }

    bool stop_requested() const override
    {
        return shell_ && shell_->stop_requested();
    }

    void destroy() override
    {
        if(ui_) ui_->destroy();
        if(shell_) shell_->destroy();
    }

private:
    std::unique_ptr<dictpen::FocusTimerUi> ui_;
    std::unique_ptr<dictpen::AppShell> shell_;
};

}  // namespace

int main()
{
    FocusTimerApplication application;
    dictpen::PlatformRuntimeOptions options;
    options.app_id = "focus_timer";
    return dictpen::run_platform_application(application, options);
}
