#include "hook_engine.h"

#include <climits>
#include <cstring>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

namespace yoyo {
namespace {

constexpr size_t kPageSize = 4096;
constexpr size_t kJumpSize = 14; /* FF 25 00 00 00 00 + abs64 */

size_t page_start(size_t value) {
    return value & ~(kPageSize - 1);
}

bool make_writable(void* address, size_t size, uintptr_t& old_state) {
#ifdef _WIN32
    DWORD old_protect = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    old_state = old_protect;
    return true;
#else
    uintptr_t start = page_start(reinterpret_cast<uintptr_t>(address));
    uintptr_t end = page_start(reinterpret_cast<uintptr_t>(address) + size - 1) + kPageSize;
    if (mprotect(reinterpret_cast<void*>(start), end - start, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }
    old_state = PROT_READ | PROT_EXEC;
    return true;
#endif
}

bool restore_protection(void* address, size_t size, uintptr_t old_state) {
#ifdef _WIN32
    DWORD old_protect = 0;
    return VirtualProtect(address, size, static_cast<DWORD>(old_state), &old_protect) != 0;
#else
    uintptr_t start = page_start(reinterpret_cast<uintptr_t>(address));
    uintptr_t end = page_start(reinterpret_cast<uintptr_t>(address) + size - 1) + kPageSize;
    return mprotect(reinterpret_cast<void*>(start), end - start, static_cast<int>(old_state)) == 0;
#endif
}

void flush_code(void* address, size_t size) {
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), address, size);
#else
    __builtin___clear_cache(static_cast<char*>(address), static_cast<char*>(address) + size);
#endif
}

void write_absolute_jump(uint8_t* at, const void* destination) {
    /* FF 25 00 00 00 00       jmp qword ptr [rip+0]
     * <64-bit destination>
     */
    at[0] = 0xFF;
    at[1] = 0x25;
    at[2] = 0x00;
    at[3] = 0x00;
    at[4] = 0x00;
    at[5] = 0x00;
    uintptr_t value = reinterpret_cast<uintptr_t>(destination);
    std::memcpy(at + 6, &value, sizeof(value));
}

void write_absolute_jump_short(uint8_t* at, const void* destination) {
    /* 48 B8 <imm64> FF E0     mov rax, imm64; jmp rax (12 bytes) */
    at[0] = 0x48;
    at[1] = 0xB8;
    uintptr_t value = reinterpret_cast<uintptr_t>(destination);
    std::memcpy(at + 2, &value, sizeof(value));
    at[10] = 0xFF;
    at[11] = 0xE0;
}

} // namespace

bool install_call_hook(void* call_site, void* detour, void** original) {
    if (call_site == nullptr || detour == nullptr || original == nullptr) {
        return false;
    }
    auto* code = static_cast<uint8_t*>(call_site);
    if (code[0] != 0xE8) {
        return false;
    }
    int32_t old_relative = 0;
    std::memcpy(&old_relative, code + 1, sizeof(old_relative));
    auto* old_target = code + 5 + old_relative;
    const auto delta = static_cast<int64_t>(reinterpret_cast<intptr_t>(detour) -
                                            reinterpret_cast<intptr_t>(code + 5));
    if (delta < INT32_MIN || delta > INT32_MAX) {
        return false;
    }
    const auto new_relative = static_cast<int32_t>(delta);

    uintptr_t old_state = 0;
    if (!make_writable(code, 5, old_state)) {
        return false;
    }
    std::memcpy(code + 1, &new_relative, sizeof(new_relative));
    flush_code(code, 5);
    restore_protection(code, 5, old_state);

    *original = old_target;
    return true;
}

void* alloc_executable(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    void* result = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return result == MAP_FAILED ? nullptr : result;
#endif
}

void* alloc_executable_near(void* near_address, size_t size) {
    if (near_address == nullptr) {
        return nullptr;
    }
    const uintptr_t target = reinterpret_cast<uintptr_t>(near_address);
#ifdef _WIN32
    const intptr_t offsets[] = {0x10000000, -0x10000000, 0x20000000, -0x20000000,
                                0x30000000, -0x30000000, 0x40000000, -0x40000000};
    for (const intptr_t offset : offsets) {
        auto hint = reinterpret_cast<void*>(target + offset);
        void* result = VirtualAlloc(hint, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (result != nullptr) {
            const auto distance = reinterpret_cast<intptr_t>(result) - static_cast<intptr_t>(target);
            if (distance > INT32_MIN && distance < INT32_MAX) {
                return result;
            }
            VirtualFree(result, 0, MEM_RELEASE);
        }
    }
    return nullptr;
#else
    const intptr_t offsets[] = {0x10000000, -0x10000000, 0x20000000, -0x20000000,
                                0x30000000, -0x30000000, 0x40000000, -0x40000000};
    for (const intptr_t offset : offsets) {
        auto hint = reinterpret_cast<void*>(target + offset);
        void* result = mmap(hint, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (result == MAP_FAILED) {
            continue;
        }
        const auto distance = reinterpret_cast<intptr_t>(result) - static_cast<intptr_t>(target);
        if (distance > INT32_MIN && distance < INT32_MAX) {
            return result;
        }
        munmap(result, size);
    }
    return nullptr;
#endif
}

void free_executable(void* ptr, size_t size) {
    if (ptr == nullptr) {
        return;
    }
#ifdef _WIN32
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

bool install_inline_hook(InlineHook& hook,
                         void* target,
                         const uint8_t* expected,
                         size_t expected_length,
                         void* detour) {
    if (target == nullptr || expected == nullptr || detour == nullptr || expected_length < kJumpSize) {
        return false;
    }
    auto* code = static_cast<uint8_t*>(target);
    if (std::memcmp(code, expected, expected_length) != 0) {
        return false;
    }

    void* trampoline = alloc_executable(expected_length + kJumpSize);
    if (trampoline == nullptr) {
        return false;
    }
    auto* tramp = static_cast<uint8_t*>(trampoline);
    std::memcpy(tramp, code, expected_length);
    write_absolute_jump(tramp + expected_length,
                        reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(target) + expected_length));

    uintptr_t old_state = 0;
    if (!make_writable(code, expected_length, old_state)) {
        free_executable(trampoline, expected_length + kJumpSize);
        return false;
    }
    std::memcpy(hook.original, code, expected_length);
    write_absolute_jump(code, detour);
    for (size_t i = kJumpSize; i < expected_length; ++i) {
        code[i] = 0x90;
    }
    flush_code(code, expected_length);
    restore_protection(code, expected_length, old_state);

    hook.target = target;
    hook.trampoline = trampoline;
    hook.patch_length = expected_length;
    hook.installed = true;
    return true;
}

bool install_inline_hook_reloc(InlineHook& hook,
                               void* target,
                               const uint8_t* expected,
                               size_t expected_length,
                               void* detour,
                               size_t disp_offset,
                               size_t instruction_length) {
    if (target == nullptr || expected == nullptr || detour == nullptr || expected_length < kJumpSize ||
        disp_offset + sizeof(int32_t) > expected_length || instruction_length < sizeof(int32_t)) {
        return false;
    }
    auto* code = static_cast<uint8_t*>(target);
    if (std::memcmp(code, expected, expected_length) != 0) {
        return false;
    }
    void* trampoline = alloc_executable_near(code, expected_length + kJumpSize);
    if (trampoline == nullptr) {
        return false;
    }
    auto* tramp = static_cast<uint8_t*>(trampoline);
    std::memcpy(tramp, code, expected_length);

    int32_t old_disp = 0;
    std::memcpy(&old_disp, code + disp_offset, sizeof(old_disp));
    const auto old_effective = reinterpret_cast<intptr_t>(code + disp_offset + sizeof(int32_t)) + old_disp;
    const auto new_disp_value =
        old_effective - (reinterpret_cast<intptr_t>(tramp + disp_offset + sizeof(int32_t)));
    if (new_disp_value < INT32_MIN || new_disp_value > INT32_MAX) {
        free_executable(trampoline, expected_length + kJumpSize);
        return false;
    }
    const auto new_disp = static_cast<int32_t>(new_disp_value);
    std::memcpy(tramp + disp_offset, &new_disp, sizeof(new_disp));
    write_absolute_jump(tramp + expected_length,
                        reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(target) + expected_length));

    uintptr_t old_state = 0;
    if (!make_writable(code, expected_length, old_state)) {
        free_executable(trampoline, expected_length + kJumpSize);
        return false;
    }
    std::memcpy(hook.original, code, expected_length);
    write_absolute_jump(code, detour);
    for (size_t i = kJumpSize; i < expected_length; ++i) {
        code[i] = 0x90;
    }
    flush_code(code, expected_length);
    restore_protection(code, expected_length, old_state);

    hook.target = target;
    hook.trampoline = trampoline;
    hook.patch_length = expected_length;
    hook.installed = true;
    return true;
}

bool install_inline_hook_short(InlineHook& hook,
                               void* target,
                               const uint8_t* expected,
                               size_t expected_length,
                               void* detour,
                               size_t jcc_offset,
                               size_t jcc_length,
                               void* jcc_target) {
    constexpr size_t kShortJumpSize = 12;
    if (target == nullptr || expected == nullptr || detour == nullptr || expected_length < kShortJumpSize) {
        return false;
    }
    if (jcc_offset != 0 && (jcc_length != 2 || jcc_offset + jcc_length > expected_length ||
                            jcc_target == nullptr)) {
        return false;
    }
    auto* code = static_cast<uint8_t*>(target);
    if (std::memcmp(code, expected, expected_length) != 0) {
        return false;
    }

    void* trampoline = alloc_executable_near(code, expected_length + 32);
    if (trampoline == nullptr) {
        return false;
    }
    auto* tramp = static_cast<uint8_t*>(trampoline);
    size_t out = 0;
    if (jcc_offset == 0) {
        std::memcpy(tramp, code, expected_length);
        out = expected_length;
    } else {
        std::memcpy(tramp, code, jcc_offset);
        out = jcc_offset;
        const uint8_t opcode = code[jcc_offset];
        if (opcode < 0x70 || opcode > 0x7F) {
            free_executable(trampoline, expected_length + 32);
            return false;
        }
        /* 0F 8x rel32: same condition code, now reaching the original
         * branch destination from the trampoline. */
        tramp[out++] = 0x0F;
        tramp[out++] = static_cast<uint8_t>(0x80 | (opcode & 0x0F));
        const auto delta = static_cast<int64_t>(reinterpret_cast<intptr_t>(jcc_target) -
                                                reinterpret_cast<intptr_t>(tramp + out + 4));
        if (delta < INT32_MIN || delta > INT32_MAX) {
            free_executable(trampoline, expected_length + 32);
            return false;
        }
        const auto relative = static_cast<int32_t>(delta);
        std::memcpy(tramp + out, &relative, sizeof(relative));
        out += 4;
        const size_t suffix = jcc_offset + jcc_length;
        std::memcpy(tramp + out, code + suffix, expected_length - suffix);
        out += expected_length - suffix;
    }
    write_absolute_jump(tramp + out, reinterpret_cast<void*>(
                                        reinterpret_cast<uintptr_t>(target) + expected_length));

    uintptr_t old_state = 0;
    if (!make_writable(code, expected_length, old_state)) {
        free_executable(trampoline, expected_length + 32);
        return false;
    }
    std::memcpy(hook.original, code, expected_length);
    write_absolute_jump_short(code, detour);
    for (size_t i = kShortJumpSize; i < expected_length; ++i) {
        code[i] = 0x90;
    }
    flush_code(code, expected_length);
    restore_protection(code, expected_length, old_state);

    hook.target = target;
    hook.trampoline = trampoline;
    hook.patch_length = expected_length;
    hook.installed = true;
    return true;
}

bool install_vtable_hook(void* object,
                         int slot,
                         void* detour,
                         void** original,
                         const uint8_t* expected,
                         size_t expected_length,
                         const char** error) {
    if (object == nullptr || original == nullptr || detour == nullptr || slot < 0 || slot > 4096) {
        if (error) {
            *error = "invalid arguments";
        }
        return false;
    }
    auto** vtable = *reinterpret_cast<void***>(object);
    if (vtable == nullptr) {
        if (error) {
            *error = "object has no vtable";
        }
        return false;
    }
    void* current = vtable[slot];
    if (current == nullptr) {
        if (error) {
            *error = "vtable slot is null";
        }
        return false;
    }
    if (expected != nullptr && expected_length > 0 && std::memcmp(current, expected, expected_length) != 0) {
        if (error) {
            *error = "vtable slot prologue mismatch";
        }
        return false;
    }

    uintptr_t old_state = 0;
    if (!make_writable(&vtable[slot], sizeof(void*), old_state)) {
        if (error) {
            *error = "cannot make vtable writable";
        }
        return false;
    }
    *original = current;
    vtable[slot] = detour;
    flush_code(&vtable[slot], sizeof(void*));
    restore_protection(&vtable[slot], sizeof(void*), old_state);
    return true;
}

} // namespace yoyo
