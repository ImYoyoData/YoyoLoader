#include "memory_span.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <cstdio>
#  include <cstring>
#  include <limits>
#endif

namespace yoyo {
namespace {

constexpr size_t kMaxSpan = 64 * 1024 * 1024;

#ifdef _WIN32
bool range_state(const void* address, size_t size, size_t& readable) {
    readable = 0;
    if (address == nullptr) {
        return false;
    }
    auto current = reinterpret_cast<const unsigned char*>(address);
    auto remaining = size;
    while (remaining > 0) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(current, &info, sizeof(info)) == 0) {
            return readable != 0;
        }
        if (info.State != MEM_COMMIT) {
            return readable != 0;
        }
        const DWORD protect = info.Protect & 0xFF;
        const bool ok = protect == PAGE_READONLY || protect == PAGE_READWRITE ||
                        protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READ ||
                        protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
        if (!ok) {
            return readable != 0;
        }
        const auto base = reinterpret_cast<const unsigned char*>(info.BaseAddress);
        const size_t available = info.RegionSize - static_cast<size_t>(current - base);
        const size_t take = available < remaining ? available : remaining;
        readable += take;
        current += take;
        remaining -= take;
        if (readable > kMaxSpan) {
            return true;
        }
    }
    return readable != 0;
}
#else
/*
 * Linux: parse /proc/self/maps once into a region list.  Doing this per query
 * would be far too slow on a hot path, and the mapping of a live allocation
 * cannot change while we hold a reference to it.
 */
struct Region {
    uintptr_t begin;
    uintptr_t end;
};

bool load_regions() {
    static Region regions[4096];
    static size_t count = 0;
    static bool loaded = false;
    if (loaded) {
        return count != 0;
    }
    loaded = true;
    std::FILE* file = std::fopen("/proc/self/maps", "r");
    if (file == nullptr) {
        return false;
    }
    char line[512];
    while (std::fgets(line, sizeof(line), file) != nullptr && count < 4096) {
        unsigned long long begin = 0;
        unsigned long long end = 0;
        char perms[8] = {};
        if (std::sscanf(line, "%llx-%llx %7s", &begin, &end, perms) != 3) {
            continue;
        }
        if (perms[0] != 'r') {
            continue;
        }
        regions[count].begin = static_cast<uintptr_t>(begin);
        regions[count].end = static_cast<uintptr_t>(end);
        ++count;
    }
    std::fclose(file);
    return count != 0;
}

bool range_state(const void* address, size_t size, size_t& readable) {
    readable = 0;
    if (address == nullptr || !load_regions()) {
        return false;
    }
    auto current = reinterpret_cast<uintptr_t>(address);
    auto remaining = size;
    /* Regions are collected in file order, which /proc/self/maps emits sorted
     * by address, so a linear walk is fine for the small count we keep. */
    for (size_t i = 0; i < 4096 && remaining > 0; ++i) {
        if (current < regions[i].begin || current >= regions[i].end) {
            continue;
        }
        const size_t available = regions[i].end - current;
        const size_t take = available < remaining ? available : remaining;
        readable += take;
        current += take;
        remaining -= take;
        if (readable > kMaxSpan) {
            return true;
        }
    }
    return readable != 0;
}
#endif

} // namespace

size_t readable_span(const void* address) {
    size_t readable = 0;
    range_state(address, kMaxSpan, readable);
    return readable;
}

bool readable_range(const void* address, size_t size) {
    if (size == 0) {
        return true;
    }
    size_t readable = 0;
    range_state(address, size, readable);
    return readable >= size;
}

} // namespace yoyo
