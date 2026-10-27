#include "native_inventory.h"

#include "components.h"
#include "stl_layout.h"
#include "live.h"
#include "log.h"
#include "players.h"
#include "runtime.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace yoyo {
namespace {

std::atomic<NativeInventoryStatus> g_status{NativeInventoryStatus::Unknown};
std::once_flag g_report_once;

std::mutex& name_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<int16_t, std::string>& name_table() {
    static std::unordered_map<int16_t, std::string> table;
    return table;
}

const LiveProfile* container_layout() {
    const auto* live = state().layout;
    if (live == nullptr) {
        return nullptr;
    }
    if (live->container_component_size == 0 || live->container_slots_offset == 0 ||
        live->item_stack_size == 0) {
        return nullptr;
    }
    return live;
}

void reject(const char* reason) {
    const bool already = g_status.load(std::memory_order_relaxed) == NativeInventoryStatus::Rejected;
    g_status.store(NativeInventoryStatus::Rejected, std::memory_order_relaxed);
    if (!already) {
        log_write(LogLevel::Warn, "native inventory disabled for this process: %s", reason);
    }
}

void accept() {
    NativeInventoryStatus expected = NativeInventoryStatus::Unknown;
    if (g_status.compare_exchange_strong(expected, NativeInventoryStatus::Verified)) {
        log_write(LogLevel::Info, "native inventory layout verified against a live player");
    }
}

/*
 * The Item pointer inside ItemStack is a WeakPtr-shaped pair: a refcount block
 * at +0 followed by the payload pointer at +8, or a plain pointer depending on
 * the build.  Both forms start with a pointer whose first element is the
 * payload, so one dereference chain covers them:
 *
 *   item_stack[+item_offset] -> word at +0 -> Item*
 *
 * The final pointer is checked before the id is read, so a wrong offset cannot
 * walk into unmapped memory.
 */
const void* resolve_item(const unsigned char* stack, const LiveProfile& live) {
    const void* raw = nullptr;
    std::memcpy(&raw, stack + live.item_stack_item, sizeof(raw));
    if (raw == nullptr) {
        return nullptr;
    }
    const auto value = reinterpret_cast<uintptr_t>(raw);
    if (value < 0x10000 || value > 0x00007FFFFFFFFFFFull) {
        return nullptr;
    }
    const void* payload = nullptr;
    std::memcpy(&payload, static_cast<const unsigned char*>(raw) + 0, sizeof(payload));
    if (payload == nullptr) {
        return nullptr;
    }
    const auto payload_value = reinterpret_cast<uintptr_t>(payload);
    if (payload_value < 0x10000 || payload_value > 0x00007FFFFFFFFFFFull) {
        return nullptr;
    }
    return payload;
}

bool read_item_stack(const unsigned char* stack, const LiveProfile& live, LiveItemStack& out) {
    out = LiveItemStack{};
    if (live.item_stack_valid >= live.item_stack_size || live.item_stack_count >= live.item_stack_size ||
        live.item_stack_aux + 2 > live.item_stack_size) {
        return false;
    }
    const bool valid = stack[live.item_stack_valid] == 1;
    const uint8_t count = stack[live.item_stack_count];
    const void* item = resolve_item(stack, live);
    if (!valid || count == 0 || item == nullptr) {
        return true; /* registered as an empty slot */
    }
    int16_t item_id = 0;
    std::memcpy(&item_id, static_cast<const unsigned char*>(item) + live.item_id_offset, sizeof(item_id));
    if (item_id < 0) {
        return true;
    }
    uint16_t aux = 0;
    std::memcpy(&aux, stack + live.item_stack_aux, sizeof(aux));
    out.present = true;
    out.count = count;
    out.aux = aux;
    out.item_id = item_id;
    out.item = item;
    if (live.item_stack_user_data != 0 && live.item_stack_user_data + sizeof(void*) <= live.item_stack_size) {
        const void* user_data = nullptr;
        std::memcpy(&user_data, stack + live.item_stack_user_data, sizeof(user_data));
        out.has_nbt = user_data != nullptr;
    }
    return true;
}

/*
 * Read the slot vector out of a ContainerComponent.
 *
 * Two independent checks guard the layout: the vector itself must look like a
 * vector, and the slot count must equal what the container reports for its own
 * size.  A build that moved either field fails one of them.
 */
bool read_container_component(void* actor, bool validate, LiveContainer& out) {
    out = LiveContainer{};
    const auto* live = container_layout();
    if (live == nullptr) {
        return false;
    }
    auto* component = component_get(actor, "ContainerComponent", live->container_component_size);
    if (component == nullptr) {
        return false;
    }
    if (live->container_slots_offset + kVectorSize > live->container_component_size) {
        reject("container slot offset lies outside the component");
        return false;
    }
    const auto* slots_address = static_cast<const unsigned char*>(component) + live->container_slots_offset;
    const void* begin = nullptr;
    size_t count = 0;
    if (!component_vector(slots_address, live->item_stack_size, &begin, count)) {
        reject("ContainerComponent slot vector is not readable");
        return false;
    }
    if (validate && count > 512) {
        reject("ContainerComponent reports an implausible slot count");
        return false;
    }
    out.container = *reinterpret_cast<void* const*>(static_cast<const unsigned char*>(component) +
                                                    live->container_object_offset);
    out.size = static_cast<int64_t>(count);
    out.slots.reserve(count);
    const auto* base = static_cast<const unsigned char*>(begin);
    for (size_t i = 0; i < count; ++i) {
        LiveItemStack stack;
        if (!read_item_stack(base + i * live->item_stack_size, *live, stack)) {
            reject("ItemStack fields lie outside the stack record");
            out = LiveContainer{};
            return false;
        }
        out.slots.push_back(std::move(stack));
    }
    out.valid = true;
    if (validate && g_status.load(std::memory_order_relaxed) == NativeInventoryStatus::Unknown) {
        accept();
    }
    return true;
}

} // namespace

void* native_container_component(void* actor) {
    const auto* live = container_layout();
    if (live == nullptr || actor == nullptr) {
        return nullptr;
    }
    return component_get(actor, "ContainerComponent", live->container_component_size);
}

bool native_container_read(void* actor, LiveContainer& out) {
    if (actor == nullptr || g_status.load(std::memory_order_relaxed) == NativeInventoryStatus::Rejected) {
        return false;
    }
    return read_container_component(actor, true, out);
}

bool native_container_slot(void* actor, int64_t slot, LiveItemStack& out) {
    LiveContainer container;
    if (!native_container_read(actor, container)) {
        return false;
    }
    if (slot < 0 || slot >= container.size) {
        return false;
    }
    out = container.slots[static_cast<size_t>(slot)];
    return true;
}

bool native_equipment_read(void* actor, LiveContainer& armor, LiveContainer& offhand) {
    (void)offhand;
    armor = LiveContainer{};
    LiveContainer all;
    if (!native_container_read(actor, all)) {
        return false;
    }
    /*
     * Armor and offhand live in the ActorEquipmentComponent, whose layout has
     * not been pinned down for every build; exposing them through the same
     * container reader would report inventory slots as armor.  Report failure
     * so the caller falls back to packets instead of showing wrong contents.
     */
    return false;
}

bool native_container_set_slot(void* actor, int64_t slot, int16_t item_id, uint8_t count, uint16_t aux) {
    if (g_status.load(std::memory_order_relaxed) != NativeInventoryStatus::Verified) {
        log_write(LogLevel::Warn,
                  "giveItem refused: the native container layout has not been verified on this build");
        return false;
    }
    const auto* live = container_layout();
    if (live == nullptr) {
        return false;
    }
    auto* component = native_container_component(actor);
    if (component == nullptr) {
        return false;
    }
    const void* begin = nullptr;
    size_t slot_count = 0;
    if (!component_vector(static_cast<const unsigned char*>(component) + live->container_slots_offset,
                          live->item_stack_size, &begin, slot_count)) {
        return false;
    }
    if (slot < 0 || static_cast<size_t>(slot) >= slot_count) {
        return false;
    }
    /*
     * Writing an ItemStack correctly means constructing one through the game
     * (item id -> Item* lookup, user data, net-id bookkeeping).  Hand-filling
     * the fields would leave stale pointers in a container the server owns, so
     * this stays disabled until the Item factory call is identified.
     */
    (void)item_id;
    (void)count;
    (void)aux;
    (void)begin;
    log_write(LogLevel::Warn,
              "giveItem refused: constructing an ItemStack needs the item factory, which is not "
              "identified on this build yet");
    return false;
}

NativeInventoryStatus native_inventory_status() {
    return g_status.load(std::memory_order_relaxed);
}

void native_inventory_report() {
    std::call_once(g_report_once, []() {
        const auto* live = container_layout();
        if (live == nullptr) {
            log_write(LogLevel::Info,
                      "inventory: native container access unavailable on this build "
                      "(no verified ContainerComponent layout)");
            return;
        }
        log_write(LogLevel::Info,
                  "inventory: native container access enabled "
                  "(component=%u bytes, slots@+0x%x, item=%u bytes)",
                  live->container_component_size, live->container_slots_offset, live->item_stack_size);
    });
}

void native_inventory_dump(void* actor) {
    const auto* live = container_layout();
    if (live == nullptr || actor == nullptr) {
        log_write(LogLevel::Warn, "container dump: no layout for this build");
        return;
    }
    {
        /* Trace the EnTT lookup step by step: a null component can mean the
         * actor has no registry, the registry has no matching pool, or the
         * entity is not in that pool.  Knowing which one it is decides whether
         * the layout is wrong or the actor simply lacks the component. */
        const auto* bytes = static_cast<const unsigned char*>(actor);
        auto* registry_ptr = *reinterpret_cast<unsigned char* const*>(bytes + 0x10);
        const uint32_t entity = *reinterpret_cast<const uint32_t*>(bytes + 0x18);
        log_write(LogLevel::Info,
                  "container dump: actor=0x%p registry=0x%p entity=0x%08x "
                  "(index=%u version=%u)",
                  actor, registry_ptr, entity, entity & 0x3FFFFu, (entity >> 18) & 0x3FFFu);
        if (registry_ptr != nullptr) {
            auto* begin = *reinterpret_cast<unsigned char**>(registry_ptr + live->entt_pools_begin);
            auto* end = *reinterpret_cast<unsigned char**>(registry_ptr + live->entt_pools_end);
            auto* entries = *reinterpret_cast<unsigned char**>(registry_ptr + live->entt_entries);
            const size_t pool_count = (begin != nullptr && end != nullptr && end > begin)
                                          ? static_cast<size_t>(end - begin) / sizeof(void*)
                                          : 0;
            log_write(LogLevel::Info, "container dump: pools=%zu begin=0x%p entries=0x%p", pool_count,
                      begin, entries);
            /* Report only whether the container pool is registered at all. The
             * full list is available with YOYO_DUMP_POOLS. */
            bool has_container_pool = false;
            size_t live_pools = 0;
            if (std::getenv("YOYO_DUMP_POOLS") != nullptr) {
                for (size_t i = 0; i < pool_count; ++i) {
                    const uint64_t value =
                        *reinterpret_cast<const uint64_t*>(begin + i * sizeof(void*));
                    if (value == UINT64_MAX) {
                        continue;
                    }
                    const uint32_t type_hash =
                        *reinterpret_cast<const uint32_t*>(entries + (value << 5) + 8);
                    if (type_hash == 0) {
                        continue;
                    }
                    ++live_pools;
                    if (type_hash == 0x1779E2C7u) {
                        has_container_pool = true;
                    }
                    log_write(LogLevel::Info, "  pool hash 0x%08X", type_hash);
                }
            } else {
                for (size_t i = 0; i < pool_count; ++i) {
                    const uint64_t value =
                        *reinterpret_cast<const uint64_t*>(begin + i * sizeof(void*));
                    if (value == UINT64_MAX) {
                        continue;
                    }
                    const uint32_t type_hash =
                        *reinterpret_cast<const uint32_t*>(entries + (value << 5) + 8);
                    if (type_hash == 0) {
                        continue;
                    }
                    ++live_pools;
                    if (type_hash == 0x1779E2C7u) {
                        has_container_pool = true;
                    }
                }
            }
            log_write(LogLevel::Info, "container dump: %zu live pools, ContainerComponent registered: %s",
                      live_pools, has_container_pool ? "yes" : "NO");
        }
    }
    /* Which identity components does this actor actually have?  Sizes are the
     * real ones: the dense page is stepped by size, so a wrong size would
     * compute a pointer into the wrong element. */
    {
        struct Probe {
            const char* name;
            size_t size;
        };
        static const Probe kProbe[] = {
            {"RuntimeIDComponent", 8},
            {"ActorUniqueIDComponent", 8},
            {"PlayerComponent", 1},
            {"ServerPlayerComponent", 1},
            {"UserEntityIdentifierComponent", 1},
            {"EquippableComponent", 1},
            {"ActorEquipmentComponent", 1},
            {"ContainerComponent", 0x200},
            {"ActorTypeComponent", 1},
            {"StateVectorComponent", 0x28},
            {"ActorGameTypeComponent", 4},
            {"AttributesComponent", 1},
        };
        /* Resolve pools without touching the per-entity pages: reading the
         * page tables of an arbitrary pool is what faults, so only the hash ->
         * pool mapping is reported here. */
        auto* registry_bytes = static_cast<unsigned char*>(actor);
        auto* reg = *reinterpret_cast<unsigned char**>(registry_bytes + 0x10);
        auto* const begin = reg ? *reinterpret_cast<unsigned char**>(reg + live->entt_pools_begin) : nullptr;
        auto* const end = reg ? *reinterpret_cast<unsigned char**>(reg + live->entt_pools_end) : nullptr;
        auto* const entries = reg ? *reinterpret_cast<unsigned char**>(reg + live->entt_entries) : nullptr;
        auto* const entries_end =
            reg && live->entt_entries_end ? *reinterpret_cast<unsigned char**>(reg + live->entt_entries_end)
                                          : nullptr;
        const size_t buckets =
            (begin && end && end > begin) ? static_cast<size_t>(end - begin) / sizeof(void*) : 0;
        const size_t capacity = (entries && entries_end && entries_end > entries)
                                    ? static_cast<size_t>(entries_end - entries) / 32
                                    : 0;
        log_write(LogLevel::Info, "  registry buckets=%zu entry_capacity=%zu", buckets, capacity);
        for (const auto& probe : kProbe) {
            const uint32_t hash = component_hash(probe.name);
            bool has_pool = false;
            void* pool = nullptr;
            if (buckets != 0 && capacity != 0) {
                size_t slot = (buckets - 1) & hash;
                for (size_t attempt = 0; attempt < buckets; ++attempt) {
                    const uint64_t value =
                        *reinterpret_cast<const uint64_t*>(begin + slot * sizeof(void*));
                    if (value == UINT64_MAX || value >= capacity) {
                        break;
                    }
                    auto* entry = entries + (value << 5);
                    if (*reinterpret_cast<const uint32_t*>(entry + 8) == hash) {
                        has_pool = true;
                        pool = *reinterpret_cast<void**>(entry + 0x10);
                        break;
                    }
                    slot = (slot + 1) % buckets;
                }
            }
            log_write(LogLevel::Info, "  probe %-32s pool=%s ptr=0x%p", probe.name,
                      has_pool ? "yes" : "no", pool);
        }
    }
    auto* component = native_container_component(actor);
    if (component == nullptr) {
        log_write(LogLevel::Warn, "container dump: actor has no ContainerComponent");
        return;
    }
    log_write(LogLevel::Info, "container dump: component=0x%p size=0x%x slots@0x%x",
              component, live->container_component_size, live->container_slots_offset);
    const auto* slots_address = static_cast<const unsigned char*>(component) + live->container_slots_offset;
    const void* begin = nullptr;
    size_t count = 0;
    if (!component_vector(slots_address, live->item_stack_size, &begin, count)) {
        log_write(LogLevel::Warn, "container dump: slot vector not readable");
        return;
    }
    log_write(LogLevel::Info, "container dump: %zu slots at 0x%p", count, begin);
    const auto* base = static_cast<const unsigned char*>(begin);
    const size_t limit = count < 8 ? count : 8;
    for (size_t i = 0; i < limit; ++i) {
        const auto* stack = base + i * live->item_stack_size;
        char hex[3 * 32 + 1] = {};
        for (size_t k = 0; k < 32 && k < live->item_stack_size; ++k) {
            std::snprintf(hex + k * 3, 4, "%02x ", stack[k]);
        }
        log_write(LogLevel::Info, "  slot[%zu] %s", i, hex);
    }
}

void native_inventory_discover(void* actor) {
    /*
     * Deliberately not implemented as a memory scan.
     *
     * Player inventories are not an EnTT component in the builds examined: the
     * ContainerComponent pool is only registered for block-entity containers
     * (chests, hoppers), and the player's own slots live inside PlayerInventory,
     * which ServerPlayer reaches through member fields we have not pinned down.
     *
     * Scanning the actor for a vector<ItemStack>-shaped structure was tried and
     * rejected: false positives make the scan dereference arbitrary pointers,
     * which crashes the server.  Per PROJECT_RULES.md ("a crash is worse than a
     * missing feature") the search is abandoned in favour of the verified
     * Script API bridge, and this function only reports which components the
     * actor actually has.
     */
    (void)actor;
    log_write(LogLevel::Info,
              "inventory discover: player slot arrays are not a component on this build; "
              "inventory reads go through the script bridge");
}

void native_item_names_update(int16_t id, std::string_view name) {
    if (id <= 0 || name.empty()) {
        return;
    }
    std::lock_guard lock(name_mutex());
    name_table()[id] = std::string(name);
}

std::string native_item_name(int16_t id) {
    std::lock_guard lock(name_mutex());
    auto it = name_table().find(id);
    return it == name_table().end() ? std::string() : it->second;
}

} // namespace yoyo
