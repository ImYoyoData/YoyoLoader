#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace yoyo {

/*
 * Cross-platform STL layout helpers.
 *
 * YoyoLoader reads and writes standard containers that live inside BDS objects
 * (Actor components, Container slot arrays, std::string fields).  It cannot
 * `#include` the game's headers, so the layouts are described here and verified
 * per platform.
 *
 * Measured on the two supported ABIs:
 *
 *                MSVC (Windows)        libc++ (Linux)
 *   std::string  32 bytes              24 bytes
 *   std::vector  24 bytes (3 pointers) 24 bytes (3 pointers)
 *   shared_ptr   16 bytes              16 bytes
 *
 * `std::vector` is three pointers in both, so one reader works everywhere.
 * `std::string` differs and goes through native_string.{h,cpp}.
 */
inline constexpr size_t kVectorSize = 24;

/*
 * A borrowed view over a game-owned std::vector<T>.  The view does not own the
 * storage: it must not outlive the vector it was created from.
 */
template <typename T>
class GameVector {
public:
    GameVector() = default;
    explicit GameVector(const void* address) {
        if (address == nullptr) {
            return;
        }
        const auto* bytes = static_cast<const unsigned char*>(address);
        std::memcpy(&begin_, bytes + 0, sizeof(begin_));
        std::memcpy(&end_, bytes + 8, sizeof(end_));
        std::memcpy(&capacity_, bytes + 16, sizeof(capacity_));
        /*
         * Reject anything that cannot be a real vector in this process: a
         * non-null begin must be followed by end >= begin, and the implied
         * element count must be sane.
         */
        const auto begin_value = reinterpret_cast<uintptr_t>(begin_);
        const auto end_value = reinterpret_cast<uintptr_t>(end_);
        const auto cap_value = reinterpret_cast<uintptr_t>(capacity_);
        if (begin_value == 0) {
            end_ = nullptr;
            capacity_ = nullptr;
            return;
        }
        if (end_value < begin_value || cap_value < end_value) {
            valid_ = false;
            return;
        }
        const auto count = (end_value - begin_value) / sizeof(T);
        if (count > 1'000'000) {
            valid_ = false;
            return;
        }
        valid_ = true;
    }

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] size_t size() const {
        if (!valid_ || begin_ == nullptr || end_ == nullptr) {
            return 0;
        }
        return static_cast<size_t>(end_ - begin_);
    }
    [[nodiscard]] bool empty() const { return size() == 0; }

    [[nodiscard]] const T& operator[](size_t index) const { return begin_[index]; }
    [[nodiscard]] T& operator[](size_t index) { return begin_[index]; }
    [[nodiscard]] const T* data() const { return begin_; }
    [[nodiscard]] const T* begin() const { return begin_; }
    [[nodiscard]] const T* end() const { return end_; }

private:
    T* begin_ = nullptr;
    T* end_ = nullptr;
    T* capacity_ = nullptr;
    bool valid_ = false;
};

/*
 * How a container stores elements, as observed in BDS.
 *
 * EnTT packs components into "dense pages": element i of a pool lives at
 * `page[(i * sizeof(T)) / page_size] + (i * sizeof(T)) % page_size`, and the
 * page table itself is addressed by a byte offset.  The existing live bridge
 * already implements that lookup; this struct documents the constants so new
 * object readers do not re-derive them.
 */
struct DensePaging {
    /* Bits of the entity index that select the sparse page. */
    uint32_t sparse_shift = 11;
    uint32_t sparse_mask = 0x7FF;
    /* Bits of the packed item that select the dense page, and the shift that
     * turns them into a byte offset into the page table. */
    uint32_t dense_page_mask = 0x3FF80;
    uint32_t dense_page_shift = 4;
    /* Bits of the packed item that select the element inside a page. */
    uint32_t dense_index_mask = 0x7F;
};

} // namespace yoyo
