#include "JSManager.hpp"

#include <stdexcept>

namespace {

Bson::object toBson(const ManagerSnapshot& snapshot)
{
    return Bson::object{
        {"success", snapshot.success},
        {"available", snapshot.available},
        {"payloadAvailable", snapshot.payloadAvailable},
        {"installed", snapshot.installed},
        {"repairRequired", snapshot.repairRequired},
        {"updateAvailable", snapshot.updateAvailable},
        {"busy", snapshot.busy},
        {"currentVersion", snapshot.currentVersion},
        {"payloadVersion", snapshot.payloadVersion},
        {"profileId", snapshot.profileId},
        {"code", snapshot.code},
        {"detail", snapshot.detail},
    };
}

void requireNoArguments(JQAsyncInfo& info, const char* operation)
{
    if(info.Length() != 0) throw std::invalid_argument(std::string(operation) + " expects no arguments");
}

}  // namespace

JSValue createManager(JQModuleEnv* env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "Manager");
    tpl->InstanceTemplate()->setObjectCreator([]() { return new JSManager(); });
    tpl->SetProtoMethodPromise("inspect", &JSManager::inspect);
    tpl->SetProtoMethodPromise("install", &JSManager::install);
    tpl->SetProtoMethodPromise("repair", &JSManager::repair);
    tpl->SetProtoMethodPromise("upgrade", &JSManager::upgrade);
    tpl->SetProtoMethodPromise("remove", &JSManager::remove);
    JSManager::InitTpl(tpl);
    return tpl->CallConstructor();
}

JSManager::JSManager()
    : manager_(new Manager())
{
}

JSManager::~JSManager() = default;

void JSManager::inspect(JQAsyncInfo& info)
{
    try {
        requireNoArguments(info, "inspect");
        info.post(toBson(manager_->inspect()));
    } catch(const std::exception& error) {
        info.postError(error.what());
    }
}

void JSManager::execute(JQAsyncInfo& info, ManagerOperation operation, const char* name)
{
    try {
        requireNoArguments(info, name);
        info.post(toBson(manager_->execute(operation)));
    } catch(const std::exception& error) {
        info.postError(error.what());
    }
}

void JSManager::install(JQAsyncInfo& info)
{
    execute(info, ManagerOperation::install, "install");
}

void JSManager::repair(JQAsyncInfo& info)
{
    execute(info, ManagerOperation::repair, "repair");
}

void JSManager::upgrade(JQAsyncInfo& info)
{
    execute(info, ManagerOperation::upgrade, "upgrade");
}

void JSManager::remove(JQAsyncInfo& info)
{
    execute(info, ManagerOperation::remove, "remove");
}
