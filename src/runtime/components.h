#pragma once

#include <cstddef>
#include <cstdint>

namespace yoyo {

/*
 * Native component access.
 *
 * BDS stores actor state in EnTT pools keyed by a 32-bit type hash.  The hash is
 * not arbitrary: it is FNV-1a/32 of the component's C++ type name, which was
 * confirmed against every hash already in use by the live bridge
 * (RuntimeIDComponent, ActorUniqueIDComponent, DimensionTypeComponent,
 * UserEntityIdentifierComponent, ActorGameTypeComponent).  Deriving hashes from
 * names therefore needs no binary scan and survives any version bump that keeps
 * the type names.
 *
 * Component storage is inline: an element lives at
 * `dense_page[index >> 7] + (index & 0x7F) * sizeof(T)`, and the index itself
 * comes from the sparse page.  That layout is identical on the MSVC and libc++
 * builds because it belongs to EnTT, not the STL.
 */

/* FNV-1a/32, the hash BDS uses for component type ids. */
constexpr uint32_t component_hash(const char* name) {
    uint32_t hash = 2166136261u;
    for (const char* p = name; *p != '\0'; ++p) {
        hash ^= static_cast<uint8_t>(*p);
        hash *= 16777619u;
    }
    return hash;
}

/* Sizes verified against 1.26.52.3 and 1.26.70.21.  A size is only ever used to
 * step the dense page, so an incorrect size reads the wrong element rather than
 * running off the page: callers additionally validate the data they read. */
namespace component_size {
constexpr size_t kRuntimeId = 8;
constexpr size_t kActorUniqueId = 8;
constexpr size_t kDimensionType = 4;
constexpr size_t kGameType = 4;
constexpr size_t kStateVector = 0x28;
constexpr size_t kActorRotation = 8;
/* ContainerComponent: 512 bytes; the slot vector sits at +0x1a8. */
constexpr size_t kContainer = 0x200;
} // namespace component_size

/* Offsets inside a component, verified by disassembly. */
namespace component_offset {
/* ContainerComponent::mContainer (+0x1a8) is a std::vector<ItemStack>. */
constexpr size_t kContainerSlots = 0x1a8;
/* ItemStack is 0x98 bytes in both builds. */
constexpr size_t kItemStackSize = 0x98;
} // namespace component_offset

/*
 * Resolve a component of `actor` by type name, or null when the actor does not
 * have it.  `size` must be the component's real size; it is used to step the
 * dense page.
 */
void* component_get(void* actor, const char* type_name, size_t size);

/*
 * Read a std::vector<T> that lives at `address` (a game-owned container).  Fills
 * `begin`/`count` and returns false when the vector is not readable.
 */
bool component_vector(const void* address, size_t element_size, const void** begin, size_t& count);

} // namespace yoyo
