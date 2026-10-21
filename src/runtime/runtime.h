#pragma once

#include "module.h"
#include "version.h"

#include <cstdint>

namespace yoyo {

struct RuntimeState {
    ModuleInfo module;
    const VersionProfile* profile = nullptr;
};

RuntimeState& state();

/* Called from DllMain / the ELF constructor. */
void runtime_start();

} // namespace yoyo
