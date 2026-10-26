#include "signatures.h"

#include "log.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <elf.h>
#endif

namespace yoyo {
namespace {

struct PatternByte {
    uint8_t value = 0;
    bool wildcard = false;
};

struct Pattern {
    std::vector<PatternByte> bytes;
    bool valid = false;
};

/* Code sections are scanned from mapped memory; no file I/O is needed. */
struct CodeRange {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uintptr_t address = 0;
};

bool pattern_from_text(const char* text, Pattern& out) {
    out.bytes.clear();
    out.valid = false;
    if (text == nullptr) {
        return false;
    }
    auto hex_value = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    const char* cursor = text;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        PatternByte byte;
        if (*cursor == '?' || *cursor == '*') {
            byte.wildcard = true;
            ++cursor;
            if (*cursor == '?') {
                ++cursor;
            }
            out.bytes.push_back(byte);
            continue;
        }
        const int high = hex_value(cursor[0]);
        if (high < 0) {
            return false;
        }
        int low = 0;
        if (cursor[1] != '\0' && cursor[1] != ' ' && cursor[1] != '\t' && cursor[1] != '\n' &&
            cursor[1] != '\r') {
            low = hex_value(cursor[1]);
            if (low < 0) {
                return false;
            }
            cursor += 2;
        } else {
            cursor += 1;
        }
        byte.wildcard = false;
        byte.value = static_cast<uint8_t>((high << 4) | low);
        out.bytes.push_back(byte);
    }
    out.valid = !out.bytes.empty();
    return out.valid;
}

#ifdef _WIN32

void collect_code_ranges(uintptr_t base, std::vector<CodeRange>& ranges) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return;
    }
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if ((section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            continue;
        }
        const size_t size = section[i].Misc.VirtualSize;
        if (size == 0) {
            continue;
        }
        ranges.push_back({reinterpret_cast<const uint8_t*>(base + section[i].VirtualAddress), size,
                          base + section[i].VirtualAddress});
    }
}

#else

void collect_code_ranges(uintptr_t base, std::vector<CodeRange>& ranges) {
    const auto* header = reinterpret_cast<const Elf64_Ehdr*>(base);
    if (header->e_ident[EI_MAG0] != ELFMAG0 || header->e_ident[EI_CLASS] != ELFCLASS64) {
        return;
    }
    const auto* phdrs = reinterpret_cast<const Elf64_Phdr*>(base + header->e_phoff);
    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        const auto& phdr = phdrs[i];
        if (phdr.p_type != PT_LOAD || (phdr.p_flags & PF_X) == 0 || phdr.p_memsz == 0) {
            continue;
        }
        ranges.push_back({reinterpret_cast<const uint8_t*>(base + phdr.p_vaddr), phdr.p_memsz,
                          base + phdr.p_vaddr});
    }
}

#endif

size_t count_in(const uint8_t* data, size_t size, const Pattern& pattern, size_t* out_offset) {
    if (!pattern.valid || size < pattern.bytes.size()) {
        return 0;
    }
    const size_t pattern_size = pattern.bytes.size();
    size_t matches = 0;
    size_t first = 0;
    for (size_t offset = 0; offset + pattern_size <= size; ++offset) {
        bool matched = true;
        for (size_t i = 0; i < pattern_size; ++i) {
            const auto& byte = pattern.bytes[i];
            if (!byte.wildcard && data[offset + i] != byte.value) {
                matched = false;
                break;
            }
        }
        if (!matched) {
            continue;
        }
        if (matches == 0) {
            first = offset;
        }
        ++matches;
        if (out_offset == nullptr && matches > 1) {
            return matches;
        }
    }
    if (matches == 1 && out_offset != nullptr) {
        *out_offset = first;
    }
    return matches;
}

/*
 * Signatures for the hook targets.  Each is anchored at the function entry and
 * covers the compiler's stack-frame setup plus the first distinctive
 * instructions.  Every immediate a rebuild may change (frame size, frame-link
 * offset, spill offsets, relative displacements) is wildcarded; only opcodes
 * and register operands are pinned.
 *
 * MSVC and clang produce different prologues, so each platform has its own
 * table.  A pattern that stops being unique fails to resolve, which disables
 * the affected feature instead of patching the wrong function.
 */
#ifdef _WIN32

const char* kTryToLoadPlayer =
    "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ?? ?? ?? ?? 48 8D AC 24 ?? ?? ?? ?? 48 C7 45 ?? FE FF FF FF "
    "4D 89 CE 4C 89 C7 48 89 D6 49 89 CF";
const char* kDisconnectClient =
    "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ?? ?? ?? ?? 48 8D AC 24 ?? ?? ?? ?? 48 C7 85 ?? ?? ?? ?? "
    "FE FF FF FF 44 89 8D ?? ?? ?? ?? 44 88 85 ?? ?? ?? ?? 48 89 95 ?? ?? ?? ?? 49 89 CF";
const char* kSetLocalPlayerInitialized =
    "41 56 56 57 53 48 81 EC ?? ?? ?? ?? 0F 29 BC 24 ?? ?? ?? ?? 0F 29 B4 24 ?? ?? ?? ?? 48 89 CE 48 83 C1 F0";
const char* kTextPacket =
    "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ?? ?? ?? ?? 48 8D AC 24 ?? ?? ?? ?? 0F 29 B5 ?? ?? ?? ?? "
    "48 C7 85 ?? ?? ?? ?? FE FF FF FF 48 8D 59";
const char* kPlayerLeft =
    "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ?? ?? ?? ?? 48 8D AC 24 ?? ?? ?? ?? 48 C7 85 ?? ?? ?? ?? "
    "FE FF FF FF 45 89 C4 48 89 D6 48 89 CB 48 8B 4A 10 8B 42 18";
const char* kLevelTick =
    "55 56 57 53 48 81 EC ?? ?? ?? ?? 48 8D AC 24 ?? ?? ?? ?? 48 C7 45 ?? FE FF FF FF 48 89 CF 8B 05";
const char* kCreatePacket = "56 48 83 EC ?? 48 89 CE 81 FA ?? ?? ?? ?? 77 ?? 89 D0 48 8D";

#else

/*
 * Linux (clang + libc++) prologues.  These are the exact bytes recorded from
 * the verified 1.26.52.3 binary, with every immediate wildcarded.  They have
 * not been checked for uniqueness on Linux (no Linux image is available in
 * this workspace), so on an unknown Linux build a pattern may match more than
 * once; when that happens resolution fails closed and the affected feature is
 * disabled rather than patching a guess.  Adding an exact profile for a new
 * Linux build re-enables everything.
 */
const char* kTryToLoadPlayer =
    "55 41 57 41 56 41 55 41 54 53 48 83 EC ?? 48 85 C0 74";
const char* kDisconnectClient =
    "55 41 57 41 56 41 55 41 54 53 48 81 EC ?? ?? ?? ?? 48 89 84 24 ?? ?? ?? ?? 48 8B 84 24";
const char* kSetLocalPlayerInitialized =
    "55 41 57 41 56 41 55 41 54 53 48 81 EC ?? ?? ?? ?? 48 89 FB 0F B6 52 10";
const char* kTextPacket =
    "55 41 57 41 56 41 55 41 54 53 48 81 EC ?? ?? ?? ?? 49 89 FD 80 BA ?? ?? ?? ?? ?? 0F 84 ?? ?? ?? ??";
const char* kPlayerLeft =
    "55 41 57 41 56 41 55 41 54 53 48 81 EC ?? ?? ?? ?? 89 D5 48 89 F3 48 8B 87";
const char* kLevelTick =
    "55 41 57 41 56 53 48 83 EC ?? 49 89 FE 0F B6 05 ?? ?? ?? ?? 84 C0";
const char* kCreatePacket =
    "53 48 89 FB 81 FE ?? ?? ?? ?? 77 ?? 89 F0 48 8D 0D ?? ?? ?? ?? 48 63 04 81 48 01 C8 FF E0";

#endif

/*
 * Turn a signature into the mask that will pin the same shape at runtime.
 * The mask is the signature itself: fixed bytes must match, wildcards are
 * free.  The expected bytes come from the reference prologue so a relocated
 * target is still validated against real instruction bytes.
 */
void fill_mask(const char* signature, size_t known_length, ResolvedTarget& target) {
    Pattern pattern;
    if (!pattern_from_text(signature, pattern)) {
        return;
    }
    /*
     * Only mask bytes we actually know.  The reference prologue is the patch
     * length and ends on an instruction boundary; the signature may be longer.
     * Masking beyond the known bytes compares against zeros and makes the
     * check fail forever.
     */
    size_t limit = pattern.bytes.size();
    if (known_length < limit) {
        limit = known_length;
    }
    if (sizeof(target.patch_bytes) < limit) {
        limit = sizeof(target.patch_bytes);
    }
    for (size_t i = 0; i < limit; ++i) {
        target.patch_mask[i] = pattern.bytes[i].wildcard ? 0 : 1;
    }
    target.patch_length = static_cast<uint8_t>(limit);
}

/*
 * Fill expected bytes from the reference prologue (when this build has one),
 * falling back to the signature's literal bytes.
 */
size_t fill_expected(const Prologue& prologue, const char* signature, ResolvedTarget& target) {
    if (prologue.length != 0) {
        const size_t limit = std::min<size_t>(prologue.length, sizeof(target.patch_bytes));
        std::memcpy(target.patch_bytes, prologue.bytes, limit);
        return limit;
    }
    Pattern pattern;
    if (!pattern_from_text(signature, pattern)) {
        return 0;
    }
    const size_t limit = std::min<size_t>(pattern.bytes.size(), sizeof(target.patch_bytes));
    for (size_t i = 0; i < limit; ++i) {
        target.patch_bytes[i] = pattern.bytes[i].value;
    }
    return limit;
}

bool prologue_matches(uintptr_t address, const ResolvedTarget& target) {
    if (address == 0 || target.patch_length == 0) {
        return false;
    }
    const auto* code = reinterpret_cast<const uint8_t*>(address);
    for (size_t i = 0; i < target.patch_length; ++i) {
        if (target.patch_mask[i] != 0 && code[i] != target.patch_bytes[i]) {
            return false;
        }
    }
    return true;
}

ResolvedTarget resolve_one(uintptr_t base, const Prologue& rva_spec, const char* signature, const char* name,
                           uint32_t rip_offset, uint32_t rip_length, ResolvedSymbols& out) {
    ResolvedTarget target;
    const size_t known = fill_expected(rva_spec, signature, target);
    fill_mask(signature, known, target);
    target.rip_offset = rip_offset;
    target.rip_instruction_length = rip_length;

    if (rva_spec.rva != 0) {
        const uintptr_t address = base + rva_spec.rva;
        if (prologue_matches(address, target)) {
            target.address = address;
            return target;
        }
        log_write(LogLevel::Warn, "%s: prologue changed at rva=0x%llx, falling back to signature", name,
                  static_cast<unsigned long long>(rva_spec.rva));
    }

    const uintptr_t found = find_signature(base, signature);
    if (found == 0) {
        log_write(LogLevel::Error, "%s: not found by signature", name);
        return {};
    }
    if (!prologue_matches(found, target)) {
        log_write(LogLevel::Error, "%s: signature matched at 0x%llx but the prologue is unexpected", name,
                  static_cast<unsigned long long>(found - base));
        return {};
    }
    log_write(LogLevel::Info, "%s: recovered by signature at rva=0x%llx", name,
              static_cast<unsigned long long>(found - base));
    target.address = found;
    target.recovered = true;
    out.recovered.emplace_back(name);
    return target;
}

} // namespace

uintptr_t find_signature(uintptr_t module_base, const char* pattern_text) {
    Pattern pattern;
    if (!pattern_from_text(pattern_text, pattern)) {
        log_write(LogLevel::Error, "invalid signature text: %s", pattern_text ? pattern_text : "<null>");
        return 0;
    }
    std::vector<CodeRange> ranges;
    collect_code_ranges(module_base, ranges);

    uintptr_t match = 0;
    size_t matches = 0;
    for (const auto& range : ranges) {
        size_t offset = 0;
        const size_t local = count_in(range.data, range.size, pattern, &offset);
        if (local == 0) {
            continue;
        }
        matches += local;
        if (local == 1) {
            if (matches == 1) {
                match = range.address + offset;
            }
        }
        if (matches > 1) {
            return 0;
        }
    }
    return matches == 1 ? match : 0;
}

size_t count_signature_matches(const uint8_t* data, size_t size, const char* pattern_text,
                               size_t* out_offset) {
    Pattern pattern;
    if (!pattern_from_text(pattern_text, pattern)) {
        return 0;
    }
    return count_in(data, size, pattern, out_offset);
}

const SignatureEntry* signature_table(size_t& count) {
    static const SignatureEntry kEntries[] = {
        {"tryToLoadPlayer", kTryToLoadPlayer},
        {"disconnectClientWithMessage", kDisconnectClient},
        {"SetLocalPlayerAsInitialized", kSetLocalPlayerInitialized},
        {"TextPacket", kTextPacket},
        {"_onPlayerLeft", kPlayerLeft},
        {"Level::tick", kLevelTick},
        {"MinecraftPackets::createPacket", kCreatePacket},
    };
    count = sizeof(kEntries) / sizeof(kEntries[0]);
    return kEntries;
}

ResolvedSymbols resolve_symbols(uintptr_t module_base, const VersionProfile* reference) {
    ResolvedSymbols out;
    if (module_base == 0) {
        return out;
    }
    out.exact = reference != nullptr;

    const Prologue none;
    const Prologue& try_load = reference != nullptr ? reference->try_to_load_player : none;
    const Prologue& disconnect = reference != nullptr ? reference->disconnect_client : none;
    const Prologue& set_init = reference != nullptr ? reference->set_local_player_initialized : none;
    const Prologue& text = reference != nullptr ? reference->text_packet : none;
    const Prologue& left = reference != nullptr ? reference->live.player_left : none;
    const Prologue& tick = reference != nullptr ? reference->live.level_tick : none;
    const Prologue& create = reference != nullptr ? reference->live.create_packet : none;
    const LiveProfile* live = reference != nullptr ? &reference->live : nullptr;

    out.try_to_load_player =
        resolve_one(module_base, try_load, kTryToLoadPlayer, "tryToLoadPlayer", 0, 0, out);
    out.disconnect_client =
        resolve_one(module_base, disconnect, kDisconnectClient, "disconnectClientWithMessage", 0, 0, out);
    out.set_local_player_initialized = resolve_one(module_base, set_init, kSetLocalPlayerInitialized,
                                                   "SetLocalPlayerAsInitialized", 0, 0, out);
    out.text_packet = resolve_one(module_base, text, kTextPacket, "TextPacket", 0, 0, out);
    out.player_left = resolve_one(module_base, left, kPlayerLeft, "_onPlayerLeft", 0, 0, out);
    out.level_tick = resolve_one(module_base, tick, kLevelTick, "Level::tick",
                                 live != nullptr ? live->level_tick_rip_offset : 0,
                                 live != nullptr ? live->level_tick_rip_instruction_length : 0, out);
    out.create_packet = resolve_one(module_base, create, kCreatePacket, "MinecraftPackets::createPacket", 0, 0,
                                    out);
    return out;
}

std::string describe_resolution(const ResolvedSymbols& symbols) {
    if (!symbols.any()) {
        return "no hook targets resolved";
    }
    if (symbols.recovered.empty()) {
        return "all resolvable hook targets matched the exact profile";
    }
    std::string text = "recovered by signature:";
    for (const auto& name : symbols.recovered) {
        text += ' ';
        text += name;
    }
    return text;
}

} // namespace yoyo
