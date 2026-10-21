#pragma once

#include <cstddef>
#include <cstdint>

namespace yoyo {

struct InlineHook {
    void* target = nullptr;
    void* trampoline = nullptr;
    size_t patch_length = 0;
    uint8_t original[32] = {};
    bool installed = false;
};

/*
 * A deliberately small x64 detour: the target prologue is replaced with an
 * absolute jump to `detour`, and a trampoline replays the overwritten bytes
 * before jumping back.  The caller supplies the exact prologue it expects;
 * if the bytes differ we do not patch anything.
 */
bool install_inline_hook(InlineHook& hook,
                         void* target,
                         const uint8_t* expected,
                         size_t expected_length,
                         void* detour);

/*
 * Replace a vtable slot.  `object` is any pointer whose first word is the
 * vtable pointer (for BDS, ServerNetworkHandler* works).  The original slot
 * is returned in `original`.
 */
bool install_vtable_hook(void* object,
                         int slot,
                         void* detour,
                         void** original,
                         const uint8_t* expected,
                         size_t expected_length,
                         const char** error);

void* alloc_executable(size_t size);
void free_executable(void* ptr, size_t size);

} // namespace yoyo
