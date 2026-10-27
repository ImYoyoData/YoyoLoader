#pragma once

#include "inventory.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yoyo {

/*
 * Native inventory access.
 *
 * Reads BDS's own ContainerComponent instead of inferring contents from
 * packets, so a plugin sees the authoritative server state even when no
 * InventoryContentPacket was sent recently.
 *
 * Layout facts established by disassembly (see docs/abiref.md):
 *
 *   ContainerComponent     0x200 bytes on Windows, 0x1a0 on Linux
 *     +container_slots     std::vector<ItemStack>
 *     +0x10                Container* (virtual setItem/getItem/...)
 *
 *   ItemStack              0x98 bytes on both platforms
 *     +0x08                Item* (refcounted; null when empty)
 *     +0x18                ItemStackNetIdVariant / user-data holder
 *     +0x20                uint16 aux value (0x7fff = not set)
 *     +0x22                uint8  count
 *     +0x23                uint8  validity flag (1 = has item)
 *     +0x78                NBT user data (shared_ptr)
 *
 * Everything is verified at runtime against a real player before being used:
 * the container must expose a slot vector whose element count matches the
 * container's own size, and each non-empty stack must have a plausible item
 * pointer.  A failed check disables the native path permanently for the
 * process rather than risking a bad read on a different build.
 */

enum class NativeInventoryStatus {
    Unknown,
    Verified,
    Rejected,
};

/* One item stack read from the live container. */
struct LiveItemStack {
    bool present = false;
    uint8_t count = 0;
    uint16_t aux = 0;
    int16_t item_id = 0;
    const void* item = nullptr;
    bool has_nbt = false;
    std::vector<uint8_t> nbt;
    std::string name;
};

struct LiveContainer {
    void* container = nullptr;   /* Container* from the component */
    int64_t size = 0;
    std::vector<LiveItemStack> slots;
    bool valid = false;
};

/* ContainerComponent on `actor`, or null when the actor has none. */
void* native_container_component(void* actor);

/* Read the whole container.  Returns false when the layout is not usable. */
bool native_container_read(void* actor, LiveContainer& out);

/* Read a single slot; `slot` is bounds-checked against the live container. */
bool native_container_slot(void* actor, int64_t slot, LiveItemStack& out);

/*
 * Write a slot.  Returns false when the layout is unverified for this build or
 * the slot index is out of range; never partially applies a write.
 */
bool native_container_set_slot(void* actor, int64_t slot, int16_t item_id, uint8_t count, uint16_t aux);

/* Armor / offhand / ender chest are the same component with a different
 * container pointer, so they share the reader. */
bool native_equipment_read(void* actor, LiveContainer& armor, LiveContainer& offhand);

NativeInventoryStatus native_inventory_status();

/*
 * Dump the raw bytes of the first few live slots to the log.
 *
 * Offsets inside ItemStack differ per build, and the only trustworthy way to
 * confirm them is to look at a stack the server actually created.  This is
 * enabled by the YOYO_DUMP_CONTAINER environment variable so it costs nothing
 * in normal operation.
 */
void native_inventory_dump(void* actor);

/*
 * Discover where a player's slot array actually lives.
 *
 * Player inventories are not an EnTT component in the builds examined so far,
 * so the offset cannot be derived from a type name.  Instead of guessing, this
 * scans the actor's own storage for a std::vector<ItemStack> whose elements
 * pass a strict validation: the stride must divide evenly by the ItemStack
 * size, every non-empty element must resolve to an Item with a plausible id,
 * and the count must be small.  A candidate that survives that is reported;
 * nothing is written.
 */
void native_inventory_discover(void* actor);
void native_inventory_report();

/*
 * Item name lookup.
 *
 * The server owns an item registry mapping runtime ids to identifiers; reading
 * it requires another layout we have not pinned down, so names come from a
 * table the bridge and packet layer feed at runtime.  Unknown ids stay numeric.
 */
void native_item_names_update(int16_t id, std::string_view name);
std::string native_item_name(int16_t id);

} // namespace yoyo
