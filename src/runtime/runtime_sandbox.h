#pragma once

#include <string>

namespace dictpen {

// Installs the mandatory application syscall filter when sessiond marks the
// process as sandboxed. Development/host runs without that marker are left
// unchanged. Returns false when a requested sandbox cannot be verified or
// installed.
bool install_runtime_sandbox(std::string& error);

}  // namespace dictpen
