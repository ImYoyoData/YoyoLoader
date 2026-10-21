#pragma once

#include <cstdint>

namespace yoyo {

struct ModuleInfo {
    uintptr_t base = 0;
    char path[1024] = {};
    char build_id[80] = {};
};

/* Resolves the main executable (bedrock_server[.exe]) and a stable build ID. */
bool init_module_info(ModuleInfo& out);

} // namespace yoyo
