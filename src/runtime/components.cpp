#include "components.h"

#include "live.h"
#include "log.h"
#include "runtime.h"

#include <cstring>

namespace yoyo {

void* component_get(void* actor, const char* type_name, size_t size) {
    if (actor == nullptr || type_name == nullptr || size == 0) {
        return nullptr;
    }
    return entt_try_get(actor, component_hash(type_name), size);
}

bool component_vector(const void* address, size_t element_size, const void** begin, size_t& count) {
    *begin = nullptr;
    count = 0;
    if (address == nullptr || element_size == 0) {
        return false;
    }
    const auto* bytes = static_cast<const unsigned char*>(address);
    const void* first = nullptr;
    const void* last = nullptr;
    const void* capacity = nullptr;
    std::memcpy(&first, bytes + 0, sizeof(first));
    std::memcpy(&last, bytes + 8, sizeof(last));
    std::memcpy(&capacity, bytes + 16, sizeof(capacity));
    if (first == nullptr) {
        /* An empty vector may still carry a null begin pointer on every ABI
         * we target; treat it as zero elements rather than as an error. */
        *begin = nullptr;
        count = 0;
        return true;
    }
    const auto first_value = reinterpret_cast<uintptr_t>(first);
    const auto last_value = reinterpret_cast<uintptr_t>(last);
    const auto capacity_value = reinterpret_cast<uintptr_t>(capacity);
    if (first_value < 0x10000 || last_value < first_value || capacity_value < last_value) {
        return false;
    }
    const auto bytes_used = last_value - first_value;
    if (bytes_used % element_size != 0) {
        return false;
    }
    const auto elements = bytes_used / element_size;
    if (elements > 1'000'000) {
        return false;
    }
    *begin = first;
    count = static_cast<size_t>(elements);
    return true;
}

} // namespace yoyo
