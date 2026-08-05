#include "JSLauncher.hpp"

#include <stdexcept>

namespace {

Bson::object toBson(const LauncherStatus& status)
{
    return Bson::object{
        {"available", status.available},
        {"accepted", status.accepted},
        {"state", status.state},
        {"supervisorPid", status.supervisorPid},
        {"appPid", status.appPid},
        {"result", status.result},
        {"message", status.message},
    };
}

void requireNoArguments(JQAsyncInfo& info, const char* operation)
{
    if(info.Length() != 0) throw std::invalid_argument(std::string(operation) + " expects no arguments");
}

}  // namespace

JSValue createLauncher(JQModuleEnv* env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "Launcher");
    tpl->InstanceTemplate()->setObjectCreator([]() { return new JSLauncher(); });
    tpl->SetProtoMethodPromise("probe", &JSLauncher::probe);
    tpl->SetProtoMethodPromise("start", &JSLauncher::start);
    tpl->SetProtoMethodPromise("status", &JSLauncher::status);
    JSLauncher::InitTpl(tpl);
    return tpl->CallConstructor();
}

JSLauncher::JSLauncher()
    : launcher_(new Launcher())
{
}

JSLauncher::~JSLauncher() = default;

void JSLauncher::probe(JQAsyncInfo& info)
{
    try {
        requireNoArguments(info, "probe");
        info.post(toBson(launcher_->probe()));
    } catch(const std::exception& error) {
        info.postError(error.what());
    }
}

void JSLauncher::start(JQAsyncInfo& info)
{
    try {
        requireNoArguments(info, "start");
        info.post(toBson(launcher_->start()));
    } catch(const std::exception& error) {
        info.postError(error.what());
    }
}

void JSLauncher::status(JQAsyncInfo& info)
{
    try {
        requireNoArguments(info, "status");
        info.post(toBson(launcher_->status()));
    } catch(const std::exception& error) {
        info.postError(error.what());
    }
}
