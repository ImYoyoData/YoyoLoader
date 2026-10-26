#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "version.h"

namespace yoyo {

/*
 * Signature-based symbol resolution.
 *
 * A signature is a byte pattern anchored at a function's entry point.  Bytes
 * that legitimately change between builds (relative call displacements,
 * stack-frame sizes, RIP-relative offsets) are wildcards, so a rebuild does
 * not invalidate the pattern.
 *
 * This is what keeps YoyoLoader usable on builds we have never seen.  The
 * hand-written RVA table in version.cpp remains the authoritative fast path
 * for known builds; everything else is found by pattern.
 */

/*
 * One resolved hook target.
 *
 * `patch_bytes` / `patch_mask` describe the bytes that must remain stable at
 * the entry point.  A patch is applied only when every non-wildcard byte
 * matches, so a function whose shape changed is refused rather than patched
 * blindly.  `patch_length` is the length of the reference prologue and is
 * assumed to end on an instruction boundary, which the wildcards do not move.
 */
struct ResolvedTarget {
    uintptr_t address = 0;
    uint8_t patch_bytes[32] = {};
    uint8_t patch_mask[32] = {};
    uint8_t patch_length = 0;
    uint32_t rip_offset = 0;
    uint32_t rip_instruction_length = 0;
    /* Set when the target came from a signature rather than the RVA table. */
    bool recovered = false;

    [[nodiscard]] bool valid() const { return address != 0; }
};

struct ResolvedSymbols {
    ResolvedTarget try_to_load_player;
    ResolvedTarget disconnect_client;
    ResolvedTarget set_local_player_initialized;
    ResolvedTarget text_packet;
    ResolvedTarget player_left;
    ResolvedTarget level_tick;
    ResolvedTarget create_packet;

    /* True when the loaded build matched an exact version profile. */
    bool exact = false;
    /* Names resolved by signature instead of the RVA table. */
    std::vector<std::string> recovered;

    bool complete() const {
        return try_to_load_player.valid() && disconnect_client.valid() &&
               set_local_player_initialized.valid() && text_packet.valid() && player_left.valid() &&
               level_tick.valid() && create_packet.valid();
    }

    /* True when at least one target resolved; partial support is allowed. */
    bool any() const {
        return try_to_load_player.valid() || disconnect_client.valid() ||
               set_local_player_initialized.valid() || text_packet.valid() || player_left.valid() ||
               level_tick.valid() || create_packet.valid();
    }
};

struct SignatureEntry {
    const char* name;
    const char* pattern;
};

/* The signature table for the current platform. */
const SignatureEntry* signature_table(size_t& count);

/*
 * Resolve every hook target.
 *
 * Preference order per target:
 *   1. the profile's RVA, when the prologue there still matches;
 *   2. otherwise the signature, accepted only when it matches exactly once
 *      and its stable bytes agree with the reference prologue.
 *
 * `reference` supplies prologue bytes and lengths; it is the exact profile
 * when the build is known, otherwise the platform's reference build.
 */
ResolvedSymbols resolve_symbols(uintptr_t module_base, const VersionProfile* reference);

/* Scan the executable's code sections. */
uintptr_t find_signature(uintptr_t module_base, const char* pattern);

/*
 * Buffer scan used by the self-test: returns the number of matches and, when
 * exactly one exists, its offset.
 */
size_t count_signature_matches(const uint8_t* data, size_t size, const char* pattern, size_t* out_offset);

std::string describe_resolution(const ResolvedSymbols& symbols);

/*
 * Structural probes.
 *
 * Entry-byte signatures break whenever the compiler changes its stack-frame
 * template -- which is exactly what happened between 1.26.52.3 and 1.26.70.21.
 * A structural probe instead looks for something *inside* the function that
 * carries meaning:
 *
 *   - a unique literal (assertion / log text) and the function referencing it;
 *   - a short instruction idiom that encodes the function's job, such as the
 *     TextPacket handler's `lea rcx,[rcx-0x10]` followed by the payload type
 *     check `cmp byte [r8+0x98], 1`.
 *
 * Each probe must match exactly one function.  An ambiguous probe disables its
 * target rather than guessing.
 */
enum class ProbeKind {
    /* `literal` is NUL-terminated text; resolve the function that loads it. */
    UniqueString,
    /* `pattern` is instruction bytes inside the function. */
    InstructionShape,
};

struct StructuralProbe {
    const char* name;
    ProbeKind kind;
    const char* pattern;
    /* InstructionShape: allowed distance from match to function entry. */
    uint32_t min_leading = 0;
    uint32_t max_leading = 0xFFFFFFFFu;
};

const StructuralProbe* structural_probes(size_t& count);

uintptr_t resolve_structural_probe(uintptr_t module_base, const StructuralProbe& probe);

} // namespace yoyo
