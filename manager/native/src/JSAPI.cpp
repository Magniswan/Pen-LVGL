#include <jsmodules/JSCModuleExtension.h>
#include <jquick_config.h>

#include "Manager/JSManager.hpp"

using namespace JQUTIL_NS;

static std::vector<std::string> exportList = {"Manager"};

static int module_init(JSContext* context, JSModuleDef* module)
{
    auto env = JQModuleEnv::CreateModule(context, module, "lvgl_manager");
    env->setModuleExport("Manager", createManager(env.get()));
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(lvgl_manager, module_init, exportList)

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("lvgl_manager", &lvgl_manager_module_load);
}
