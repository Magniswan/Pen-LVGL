#include "JSLauncher.hpp"

#include <stdexcept>

namespace {

Bson::object toBson(const LauncherStatus& status)
{
    return Bson::object{
        {"available", status.available},
        {"accepted", status.accepted},
        {"holeReady", status.holeReady},
        {"inputReady", status.inputReady},
        {"logicalWidth", status.logicalWidth},
        {"logicalHeight", status.logicalHeight},
        {"state", status.state},
        {"sessionPid", status.sessionPid},
        {"result", status.result},
        {"message", status.message},
    };
}

int requiredInteger(const Bson::object& values, const char* name, int minimum, int maximum)
{
    const auto found = values.find(name);
    if(found == values.end() || !found->second.is_number()) {
        throw std::invalid_argument(std::string(name) + " must be a number");
    }
    const int value = found->second.int_value();
    if(value < minimum || value > maximum) {
        throw std::invalid_argument(std::string(name) + " is out of range");
    }
    return value;
}

std::string requiredString(const Bson::object& values, const char* name)
{
    const auto found = values.find(name);
    if(found == values.end() || !found->second.is_string()) {
        throw std::invalid_argument(std::string(name) + " must be a string");
    }
    return found->second.string_value();
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
    tpl->SetProtoMethod("sendTouch", &JSLauncher::sendTouch);
    JSLauncher::InitTpl(tpl);
    return tpl->CallConstructor();
}

void JSLauncher::sendTouch(JQFunctionInfo& info)
{
    try {
        if(info.Length() != 1) throw std::invalid_argument("sendTouch expects one object");
        const Bson payload = JSValueToBson(info.GetContext(), info[0]);
        if(!payload.is_object()) throw std::invalid_argument("sendTouch expects one object");
        const auto& values = payload.object_items();
        TouchRequest request;
        request.phase = requiredString(values, "phase");
        request.contactId = static_cast<std::uint32_t>(requiredInteger(values, "contactId", 0, 31));
        request.x = requiredInteger(values, "x", 0, 4095);
        request.y = requiredInteger(values, "y", 0, 2047);
        std::string error;
        if(!launcher_->sendTouch(request, error)) {
            info.GetReturnValue().ThrowInternalError(error.c_str());
            return;
        }
        info.GetReturnValue().Set(true);
    } catch(const std::exception& error) {
        info.GetReturnValue().ThrowInternalError(error.what());
    }
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
