#pragma once

#include "Manager.hpp"

#include <jqutil_v2/jqutil.h>
#include <memory>

using namespace JQUTIL_NS;

class JSManager : public JQPublishObject {
public:
    JSManager();
    ~JSManager();

    void inspect(JQAsyncInfo& info);
    void install(JQAsyncInfo& info);
    void repair(JQAsyncInfo& info);
    void upgrade(JQAsyncInfo& info);
    void remove(JQAsyncInfo& info);

private:
    void execute(JQAsyncInfo& info, ManagerOperation operation, const char* name);
    std::unique_ptr<Manager> manager_;
};

extern JSValue createManager(JQModuleEnv* env);
