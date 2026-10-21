#include "native_string.h"

#include <cstdint>
#include <cstring>

namespace yoyo {
namespace {

constexpr size_t kMaxString = 4096;

bool plausible(const void* data, size_t size) {
    return data != nullptr && size <= kMaxString && reinterpret_cast<uintptr_t>(data) > 0x10000;
}

} // namespace

std::string_view read_native_string(const void* p, NativeAbi abi) {
    if (p == nullptr) {
        return {};
    }
    const auto* base = static_cast<const unsigned char*>(p);
    const char* data = nullptr;
    size_t size = 0;

    if (abi == NativeAbi::Msvc) {
        /*
         * MSVC x64 std::string:
         *   +0   union { char buf[16]; char* ptr; }
         *   +16  size_t size
         *   +24  size_t capacity
         */
        std::memcpy(&size, base + 16, sizeof(size));
        size_t capacity = 0;
        std::memcpy(&capacity, base + 24, sizeof(capacity));
        if (capacity >= 16) {
            std::memcpy(&data, base, sizeof(data));
        } else {
            data = reinterpret_cast<const char*>(base);
        }
    } else {
        /*
         * libc++ (LLVM 20) std::string:
         *   short: byte 0 = size << 1 (bit 0 == 0), data at +1
         *   long:  bit 0 == 1, size at +8, data pointer at +16
         */
        unsigned char flag = base[0];
        if ((flag & 1u) != 0) {
            std::memcpy(&size, base + 8, sizeof(size));
            std::memcpy(&data, base + 16, sizeof(data));
        } else {
            size = static_cast<size_t>(flag >> 1);
            data = reinterpret_cast<const char*>(base + 1);
        }
    }

    if (!plausible(data, size)) {
        return {};
    }
    return std::string_view(data, size);
}

bool write_native_string(void* p, std::string_view value, NativeAbi abi) {
    if (p == nullptr || value.size() > kMaxString) {
        return false;
    }
    auto* base = static_cast<unsigned char*>(p);

    if (abi == NativeAbi::Msvc) {
        size_t capacity = 0;
        std::memcpy(&capacity, base + 24, sizeof(capacity));
        char* data = nullptr;
        size_t max_length = 0;
        if (capacity >= 16) {
            std::memcpy(&data, base, sizeof(data));
            max_length = capacity;
        } else {
            data = reinterpret_cast<char*>(base);
            max_length = 15;
        }
        if (data == nullptr || value.size() > max_length) {
            return false;
        }
        std::memcpy(data, value.data(), value.size());
        data[value.size()] = '\0';
        size_t size = value.size();
        std::memcpy(base + 16, &size, sizeof(size));
        return true;
    }

    unsigned char flag = base[0];
    if ((flag & 1u) == 0) {
        if (value.size() > 22) {
            return false;
        }
        base[0] = static_cast<unsigned char>(value.size() << 1);
        std::memcpy(base + 1, value.data(), value.size());
        base[1 + value.size()] = '\0';
        return true;
    }

    uint64_t capacity_field = 0;
    std::memcpy(&capacity_field, base, sizeof(capacity_field));
    char* data = nullptr;
    std::memcpy(&data, base + 16, sizeof(data));
    if (data == nullptr || capacity_field < 3) {
        return false;
    }
    size_t max_length = static_cast<size_t>(capacity_field - 2);
    if (value.size() > max_length) {
        return false;
    }
    std::memcpy(data, value.data(), value.size());
    data[value.size()] = '\0';
    size_t size = value.size();
    std::memcpy(base + 8, &size, sizeof(size));
    return true;
}

} // namespace yoyo
