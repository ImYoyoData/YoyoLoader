#include "inventory.h"

#include "live.h"
#include "log.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

namespace yoyo {
namespace {

/*
 * Wire reader.  Every read is bounds-checked: a malformed packet must produce
 * "cannot parse", never an out-of-bounds read.
 */
class Reader {
public:
    Reader(const std::vector<uint8_t>& data, size_t offset = 0) : data_(data), offset_(offset) {}

    [[nodiscard]] size_t position() const { return offset_; }
    [[nodiscard]] size_t remaining() const { return data_.size() - offset_; }
    [[nodiscard]] bool ok() const { return ok_; }

    uint8_t u8() {
        if (offset_ + 1 > data_.size()) {
            ok_ = false;
            return 0;
        }
        return data_[offset_++];
    }

    uint16_t u16() {
        if (offset_ + 2 > data_.size()) {
            ok_ = false;
            return 0;
        }
        uint16_t value = 0;
        std::memcpy(&value, data_.data() + offset_, sizeof(value));
        offset_ += 2;
        return value;
    }

    int16_t i16() { return static_cast<int16_t>(u16()); }

    int32_t i32() {
        if (offset_ + 4 > data_.size()) {
            ok_ = false;
            return 0;
        }
        int32_t value = 0;
        std::memcpy(&value, data_.data() + offset_, sizeof(value));
        offset_ += 4;
        return value;
    }

    uint32_t varint() {
        uint32_t value = 0;
        int shift = 0;
        while (shift <= 28) {
            const uint8_t byte = u8();
            if (!ok_) {
                return 0;
            }
            value |= static_cast<uint32_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return value;
            }
            shift += 7;
        }
        ok_ = false;
        return 0;
    }

    int32_t zigzag32() {
        const uint32_t raw = varint();
        return static_cast<int32_t>((raw >> 1) ^ (~(raw & 1) + 1));
    }

    std::vector<uint8_t> bytes(size_t count) {
        if (offset_ + count > data_.size()) {
            ok_ = false;
            return {};
        }
        std::vector<uint8_t> out(data_.begin() + offset_, data_.begin() + offset_ + count);
        offset_ += count;
        return out;
    }

    void skip(size_t count) {
        if (offset_ + count > data_.size()) {
            ok_ = false;
            return;
        }
        offset_ += count;
    }

    void skip_string() {
        const uint32_t length = varint();
        if (ok_) {
            skip(length);
        }
    }

private:
    const std::vector<uint8_t>& data_;
    size_t offset_ = 0;
    bool ok_ = true;
};

std::mutex& cache_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<int32_t, ContainerSnapshot>& cache() {
    static std::unordered_map<int32_t, ContainerSnapshot> table;
    return table;
}

std::unordered_map<int16_t, std::string>& name_table() {
    static std::unordered_map<int16_t, std::string> names;
    return names;
}

} // namespace

bool parse_item_v4(const std::vector<uint8_t>& data, size_t& offset, ItemStackView& out) {
    Reader reader(data, offset);
    ItemStackView item;

    item.network_id = reader.i16();
    item.count = reader.u16();
    item.metadata = static_cast<int32_t>(reader.varint());

    const bool has_stack_id = reader.u8() != 0;
    if (has_stack_id) {
        (void)reader.zigzag32();
    }
    item.block_runtime_id = static_cast<int32_t>(reader.varint());

    const uint32_t extra_length = reader.varint();
    if (!reader.ok() || extra_length > reader.remaining()) {
        return false;
    }
    const size_t extra_start = reader.position();
    reader.skip(extra_length);
    if (!reader.ok()) {
        return false;
    }

    if (item.network_id != 0 && extra_length > 0) {
        /* Decode the extra block from its own slice. */
        std::vector<uint8_t> extra(data.begin() + extra_start, data.begin() + extra_start + extra_length);
        Reader extra_reader(extra);
        const uint16_t has_nbt = extra_reader.u16();
        if (has_nbt == 0xFFFF) {
            const uint8_t version = extra_reader.u8();
            (void)version;
            /*
             * The NBT body length is not framed, so the remaining extra bytes
             * cannot be split reliably here.  Keep the whole remainder: callers
             * that need structure use YoyoLoader's NBT parser on it.
             */
            item.nbt = extra_reader.bytes(extra_reader.remaining());
            item.has_nbt = !item.nbt.empty();
        }
    }

    offset = reader.position();
    out = std::move(item);
    return true;
}

bool parse_inventory_content(const std::vector<uint8_t>& payload, ContainerSnapshot& out) {
    Reader reader(payload);
    ContainerSnapshot snapshot;
    snapshot.window_id = static_cast<int32_t>(reader.varint());
    if (!reader.ok()) {
        return false;
    }
    const uint32_t count = reader.varint();
    if (!reader.ok() || count > 4096) {
        return false;
    }
    snapshot.slots.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        size_t offset = reader.position();
        ItemStackView item;
        if (!parse_item_v4(payload, offset, item)) {
            return false;
        }
        reader.skip(offset - reader.position());
        snapshot.slots.push_back(std::move(item));
    }
    snapshot.valid = reader.ok();
    if (!snapshot.valid) {
        return false;
    }
    out = std::move(snapshot);
    return true;
}

bool parse_inventory_slot(const std::vector<uint8_t>& payload, int32_t& window_id, int32_t& slot,
                          ItemStackView& out) {
    Reader reader(payload);
    window_id = static_cast<int32_t>(reader.varint());
    slot = static_cast<int32_t>(reader.varint());
    if (!reader.ok()) {
        return false;
    }
    size_t offset = reader.position();
    return parse_item_v4(payload, offset, out);
}

void inventory_cache_store(const ContainerSnapshot& snapshot) {
    if (!snapshot.valid) {
        return;
    }
    std::lock_guard lock(cache_mutex());
    cache()[snapshot.window_id] = snapshot;
}

void inventory_cache_store_slot(int32_t window_id, int32_t slot, const ItemStackView& item) {
    if (slot < 0 || slot > 4096) {
        return;
    }
    std::lock_guard lock(cache_mutex());
    auto& snapshot = cache()[window_id];
    snapshot.window_id = window_id;
    if (static_cast<size_t>(slot) >= snapshot.slots.size()) {
        snapshot.slots.resize(static_cast<size_t>(slot) + 1);
    }
    snapshot.slots[static_cast<size_t>(slot)] = item;
    snapshot.valid = true;
}

bool inventory_cache_get(int32_t window_id, ContainerSnapshot& out) {
    std::lock_guard lock(cache_mutex());
    auto it = cache().find(window_id);
    if (it == cache().end() || !it->second.valid) {
        return false;
    }
    out = it->second;
    return true;
}

void inventory_cache_clear() {
    std::lock_guard lock(cache_mutex());
    cache().clear();
}

void inventory_register_name(int16_t network_id, std::string_view name) {
    if (network_id == 0 || name.empty()) {
        return;
    }
    std::lock_guard lock(cache_mutex());
    name_table()[network_id] = std::string(name);
}

std::string inventory_name_for(int16_t network_id) {
    std::lock_guard lock(cache_mutex());
    auto it = name_table().find(network_id);
    return it == name_table().end() ? std::string() : it->second;
}

bool inventory_give_item(void* player, int16_t network_id, uint16_t count, int32_t metadata) {
    /*
     * There is no safe way to add an item to the *server* inventory without the
     * Container ABI.  What can be done without risk is refreshing the client's
     * view from our cache; giving a real item requires the server-side call, so
     * this reports failure instead of pretending.
     */
    (void)player;
    (void)network_id;
    (void)count;
    (void)metadata;
    log_write(LogLevel::Warn,
              "giveItem needs the Container ABI; the item was not added to the server inventory");
    return false;
}

} // namespace yoyo
