#pragma once

#include "Launcher.hpp"

#include <jqutil_v2/jqutil.h>
#include <memory>

using namespace JQUTIL_NS;

class JSLauncher : public JQPublishObject {
public:
    JSLauncher();
    ~JSLauncher();

    void probe(JQAsyncInfo& info);
    void start(JQAsyncInfo& info);
    void status(JQAsyncInfo& info);
    void sendTouch(JQFunctionInfo& info);

private:
    std::unique_ptr<Launcher> launcher_;
};

extern JSValue createLauncher(JQModuleEnv* env);
