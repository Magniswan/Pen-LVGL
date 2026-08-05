#include <jsmodules/JSCModuleExtension.h>
#include <jquick_config.h>

#include "Launcher/JSLauncher.hpp"

using namespace JQUTIL_NS;

static std::vector<std::string> exportList = {"Launcher"};

static int module_init(JSContext* context, JSModuleDef* module)
{
    auto env = JQModuleEnv::CreateModule(context, module, "lvgl_launcher");
    env->setModuleExport("Launcher", createLauncher(env.get()));
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(lvgl_launcher, module_init, exportList)

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("lvgl_launcher", &lvgl_launcher_module_load);
}
