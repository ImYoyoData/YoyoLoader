#pragma once

#include <cstddef>
#include <cstdint>

namespace yoyo {

/*
 * How many bytes starting at `address` can be read without faulting.
 *
 * BDS stores packet payloads in std::vector-backed buffers whose length we
 * cannot see from the packet header, and the objects that own them are only
 * reachable through pointers we read out of game memory.  Parsing such a buffer
 * therefore needs a bound that comes from the memory map rather than from a
 * guessed constant: reading 8 KiB past the end of a 200-byte allocation is what
 * crashed the server in earlier attempts.
 *
 * The returned span never crosses into an unmapped or non-readable region, so
 * callers may read up to (but not including) it.  Returns 0 when the address
 * itself is not readable.
 */
size_t readable_span(const void* address);

/*
 * True when the whole range [address, address + size) is readable.
 */
bool readable_range(const void* address, size_t size);

} // namespace yoyo
