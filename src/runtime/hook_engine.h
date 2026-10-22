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

/*
 * Redirect one direct `E8 rel32` call site to `detour`.  The original
 * destination is returned in `original`; the detour can call it as a normal
 * function.  Used for hot functions whose entry cannot be copied safely.
 */
bool install_call_hook(void* call_site, void* detour, void** original);

/*
 * Install an inline hook whose copied prologue contains one RIP-relative
 * instruction (`disp_offset` points at its 4-byte displacement).  The
 * trampoline gets a relocated displacement.
 */
bool install_inline_hook_reloc(InlineHook& hook,
                               void* target,
                               const uint8_t* expected,
                               size_t expected_length,
                               void* detour,
                               size_t disp_offset,
                               size_t instruction_length);

/*
 * Install an inline hook when the shortest safe prologue is 12 bytes (a
 * 12-byte `mov rax, imm64; jmp rax` patch is used instead of the usual
 * 14-byte indirect jump).  If `jcc_offset` is not zero, the 2-byte short
 * conditional branch at that offset is rewritten in the trampoline as a
 * 6-byte near branch to `jcc_target`.
 */
bool install_inline_hook_short(InlineHook& hook,
                               void* target,
                               const uint8_t* expected,
                               size_t expected_length,
                               void* detour,
                               size_t jcc_offset = 0,
                               size_t jcc_length = 0,
                               void* jcc_target = nullptr);

void* alloc_executable(size_t size);
void* alloc_executable_near(void* near_address, size_t size);
void free_executable(void* ptr, size_t size);

} // namespace yoyo
