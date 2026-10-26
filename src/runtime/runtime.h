#pragma once

#include "module.h"
#include "signatures.h"
#include "version.h"

#include <cstdint>

namespace yoyo {

struct RuntimeState {
    ModuleInfo module;
    const VersionProfile* profile = nullptr;
    /*
     * Absolute hook targets in the loaded process.  They come from the exact
     * version profile when the build is known, otherwise from signature
     * scanning, so a new build still works without a hand-written RVA table.
     */
    ResolvedSymbols symbols;
    /*
     * Data-layout profile used for object offsets and packet field offsets.
     * A known build uses its own entry; an unknown build reuses the layout of
     * the closest known build and flags it so callers can avoid risky writes.
     */
    const LiveProfile* layout = nullptr;
    bool layout_fallback = false;
};

RuntimeState& state();

/* Called from DllMain / the ELF constructor. */
void runtime_start();

} // namespace yoyo
