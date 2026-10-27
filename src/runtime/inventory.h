#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace yoyo {

/*
 * Item stacks as they appear in inventory packets.
 *
 * The wire layout below was taken from the Bedrock protocol definition for
 * 1.26.x (minecraft-data), not guessed:
 *
 *   Item:
 *     int16   network_id        (0 = empty slot)
 *     uint16  count
 *     varint  metadata
 *     bool    has_stack_id
 *     zigzag32 stack_id          (only when has_stack_id)
 *     varint  block_runtime_id
 *     bytes   extra              (varint-length prefixed)
 *
 *   extra:
 *     uint16  has_nbt (0xFFFF = yes)
 *     [u8 version + NBT]         (only when has_nbt)
 *     int32   can_place_on count ; ShortString[] ...
 *     int32   can_destroy count ; ShortString[] ...
 *     [int64 blocking_tick]      (shield only)
 *
 * Names are resolved through the server's own item registry, which we do not
 * link against; callers get the numeric id and, when known, a cached name.
 */
struct ItemStackView {
    int16_t network_id = 0;   /* 0 means empty */
    uint16_t count = 0;
    int32_t metadata = 0;
    int32_t block_runtime_id = 0;
    /* Raw NBT payload (little-endian compound), empty when the item has none. */
    std::vector<uint8_t> nbt;
    bool has_nbt = false;
    /* Filled by the cache when the id was seen with a known name. */
    std::string type_name;

    [[nodiscard]] bool empty() const { return network_id == 0 || count == 0; }
};

/* Slot contents of a container, keyed by slot index. */
struct ContainerSnapshot {
    /* window id from the packet: 0 inventory, 119 offhand, 120 armor, ... */
    int32_t window_id = 0;
    std::vector<ItemStackView> slots;
    bool valid = false;
};

/*
 * Parse an ItemV4 from `data` at `offset`.  Returns false when the buffer ends
 * early or a length field is implausible; the caller must not trust `out` then.
 */
bool parse_item_v4(const std::vector<uint8_t>& data, size_t& offset, ItemStackView& out);

/*
 * Parse an InventoryContentPacket payload into slots.
 * Returns false on malformed data.
 */
bool parse_inventory_content(const std::vector<uint8_t>& payload, ContainerSnapshot& out);

/* Parse an InventorySlotPacket payload (window id, slot index, item). */
bool parse_inventory_slot(const std::vector<uint8_t>& payload, int32_t& window_id, int32_t& slot,
                          ItemStackView& out);

/*
 * Inventory cache.
 *
 * The server sends InventoryContent/InventorySlot when a container changes; the
 * cache keeps the latest state per window so plugins can read an inventory
 * without holding a live Container pointer.
 */
void inventory_cache_store(const ContainerSnapshot& snapshot);
void inventory_cache_store_slot(int32_t window_id, int32_t slot, const ItemStackView& item);
bool inventory_cache_get(int32_t window_id, ContainerSnapshot& out);
void inventory_cache_clear();

/*
 * Give an item to a player by sending the client-side inventory update.
 *
 * This changes what the client sees and reports; it does not run the server's
 * pickup/give pipeline.  Returns false when the slot cannot be addressed.
 */
bool inventory_give_item(void* player, int16_t network_id, uint16_t count, int32_t metadata);

/* Resolve a block/item type name for an id, once the registry has been probed. */
void inventory_register_name(int16_t network_id, std::string_view name);
std::string inventory_name_for(int16_t network_id);

} // namespace yoyo
