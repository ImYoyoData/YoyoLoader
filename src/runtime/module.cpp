#include "module.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <elf.h>
#  include <fcntl.h>
#  include <link.h>
#  include <unistd.h>
#endif

namespace yoyo {
namespace {

void to_hex(const unsigned char* data, size_t size, char* out, size_t out_size) {
    static const char* digits = "0123456789abcdef";
    size_t pos = 0;
    for (size_t i = 0; i < size && pos + 2 < out_size; ++i) {
        out[pos++] = digits[data[i] >> 4];
        out[pos++] = digits[data[i] & 0x0f];
    }
    out[pos] = '\0';
}

uint64_t fnv1a_file(const char* path) {
    uint8_t buffer[64 * 1024];
    uint64_t hash = 1469598103934665603ull;
#ifdef _WIN32
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        for (DWORD i = 0; i < read; ++i) {
            hash ^= buffer[i];
            hash *= 1099511628211ull;
        }
    }
    CloseHandle(file);
#else
    int file = ::open(path, O_RDONLY);
    if (file < 0) {
        return 0;
    }
    ssize_t read = 0;
    while ((read = ::read(file, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < read; ++i) {
            hash ^= buffer[i];
            hash *= 1099511628211ull;
        }
    }
    ::close(file);
#endif
    return hash;
}

} // namespace

#ifdef _WIN32

static bool parse_pe_build_id(uintptr_t base, ModuleInfo& out) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (dir.VirtualAddress == 0 || dir.Size < sizeof(IMAGE_DEBUG_DIRECTORY)) {
        return false;
    }
    const auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + dir.VirtualAddress);
    size_t count = dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
    for (size_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];
        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.AddressOfRawData == 0 || entry.SizeOfData < 24) {
            continue;
        }
        const auto* data = reinterpret_cast<const unsigned char*>(base + entry.AddressOfRawData);
        uint32_t signature = 0;
        std::memcpy(&signature, data, 4);
        if (signature != 0x53445352) { /* 'RSDS' */
            continue;
        }
        const unsigned char* guid = data + 4;
        uint32_t age = 0;
        std::memcpy(&age, data + 20, sizeof(age));

        unsigned d1 = guid[0] | (guid[1] << 8) | (guid[2] << 16) | (guid[3] << 24);
        unsigned d2 = guid[4] | (guid[5] << 8);
        unsigned d3 = guid[6] | (guid[7] << 8);
        char guid_text[40];
        std::snprintf(guid_text, sizeof(guid_text), "%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
                      d1, d2, d3, guid[8], guid[9], guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
        std::snprintf(out.build_id, sizeof(out.build_id), "%s%x", guid_text, age);
        return true;
    }
    return false;
}

bool init_module_info(ModuleInfo& out) {
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module) {
        return false;
    }
    out.base = reinterpret_cast<uintptr_t>(module);

    wchar_t wide_path[1024] = {};
    DWORD length = GetModuleFileNameW(nullptr, wide_path, 1024);
    if (length == 0 || length >= 1024) {
        return false;
    }
    int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide_path, static_cast<int>(length), out.path,
                                          sizeof(out.path) - 1, nullptr, nullptr);
    if (utf8_length <= 0) {
        return false;
    }
    out.path[utf8_length] = '\0';

    if (!parse_pe_build_id(out.base, out)) {
        uint64_t hash = fnv1a_file(out.path);
        std::snprintf(out.build_id, sizeof(out.build_id), "fnv:%016llx", static_cast<unsigned long long>(hash));
    }
    return true;
}

#else

struct PhdrSearch {
    uintptr_t base = 0;
    bool found = false;
};

static int find_main_phdr(dl_phdr_info* info, size_t, void* data) {
    auto* search = static_cast<PhdrSearch*>(data);
    if (info->dlpi_name == nullptr || info->dlpi_name[0] == '\0') {
        search->base = static_cast<uintptr_t>(info->dlpi_addr);
        search->found = true;
        return 1;
    }
    return 0;
}

static bool parse_elf_build_id(uintptr_t base, ModuleInfo& out) {
    const auto* header = reinterpret_cast<const Elf64_Ehdr*>(base);
    if (header->e_ident[EI_MAG0] != ELFMAG0 || header->e_ident[EI_MAG1] != ELFMAG1 ||
        header->e_ident[EI_MAG2] != ELFMAG2 || header->e_ident[EI_MAG3] != ELFMAG3 ||
        header->e_ident[EI_CLASS] != ELFCLASS64) {
        return false;
    }
    const auto* phdrs = reinterpret_cast<const Elf64_Phdr*>(base + header->e_phoff);
    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        const auto& phdr = phdrs[i];
        if (phdr.p_type != PT_NOTE || phdr.p_memsz == 0) {
            continue;
        }
        auto cursor = reinterpret_cast<const unsigned char*>(base + phdr.p_vaddr);
        auto end = cursor + phdr.p_memsz;
        while (cursor + sizeof(Elf64_Nhdr) <= end) {
            const auto* note = reinterpret_cast<const Elf64_Nhdr*>(cursor);
            uintptr_t name_start = reinterpret_cast<uintptr_t>(cursor + sizeof(Elf64_Nhdr));
            uintptr_t desc_start = (name_start + note->n_namesz + 3u) & ~uintptr_t(3u);
            uintptr_t next = (desc_start + note->n_descsz + 3u) & ~uintptr_t(3u);
            if (next > reinterpret_cast<uintptr_t>(end)) {
                break;
            }
            if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 &&
                std::memcmp(reinterpret_cast<const void*>(name_start), "GNU", 4) == 0) {
                to_hex(reinterpret_cast<const unsigned char*>(desc_start), note->n_descsz, out.build_id,
                       sizeof(out.build_id));
                return true;
            }
            cursor = reinterpret_cast<const unsigned char*>(next);
        }
    }
    return false;
}

bool init_module_info(ModuleInfo& out) {
    PhdrSearch search;
    dl_iterate_phdr(find_main_phdr, &search);
    if (!search.found) {
        return false;
    }
    out.base = search.base;

    ssize_t length = readlink("/proc/self/exe", out.path, sizeof(out.path) - 1);
    if (length <= 0) {
        return false;
    }
    out.path[length] = '\0';

    if (!parse_elf_build_id(out.base, out)) {
        uint64_t hash = fnv1a_file(out.path);
        std::snprintf(out.build_id, sizeof(out.build_id), "fnv:%016llx", static_cast<unsigned long long>(hash));
    }
    return true;
}

#endif

} // namespace yoyo
