#include "client_commands.h"

#include "js/js_engine.h"
#include "live.h"
#include "log.h"
#include "players.h"
#include "runtime.h"

#include <cstring>
#include <mutex>
#include <vector>

namespace yoyo {
namespace {

constexpr int kPacketAvailableCommands = 76;
constexpr size_t kPacketBaseSize = 48;

std::mutex& command_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<ClientCommand>& plugin_commands() {
    static std::vector<ClientCommand> commands;
    return commands;
}

/* Raw payload the server sent, kept so vanilla commands survive a merge. */
struct CapturedTable {
    std::vector<uint8_t> payload;
    bool valid = false;
};

CapturedTable& captured() {
    static CapturedTable table;
    return table;
}

/* ---------- wire helpers ---------- */

void put_varint(std::vector<uint8_t>& out, uint32_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

void put_string(std::vector<uint8_t>& out, std::string_view text) {
    put_varint(out, static_cast<uint32_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

void put_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void put_u32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

/*
 * Packet builder.
 *
 * Layout of AvailableCommandsPacket (1.26.x):
 *   varint  values_len
 *   string  enum_values[values_len]
 *   varint  chained_subcommand_values_len ; string[] ...
 *   varint  suffixes_len ; string[] ...
 *   varint  enums_len ; { string name, varint count, lu32 values[] } ...
 *   varint  chained_subcommands_len ; ...
 *   varint  commands_len ; { string name, string description,
 *                            u16 flags, u8 permission, li32 alias_enum,
 *                            varint overloads_len ; ... } ...
 *   varint  dynamic_enums_len ; ...
 *   varint  enum_constraints_len ; ...
 *
 * Rather than re-implement every sub-structure, the captured vanilla payload is
 * reused verbatim and only the command array is spliced: we read its offset and
 * append our entries, then re-emit the tail sections untouched.  That keeps us
 * compatible with whatever the server sent, including new fields.
 */
struct SplicedTable {
    std::vector<uint8_t> prefix;      /* up to commands_len */
    std::vector<uint8_t> command_blob;/* the existing commands, verbatim */
    std::vector<uint8_t> suffix;      /* dynamic_enums + enum_constraints */
    uint32_t command_count = 0;
    bool ok = false;
};

uint32_t read_varint(const std::vector<uint8_t>& data, size_t& offset, bool& ok) {
    uint32_t value = 0;
    int shift = 0;
    while (offset < data.size() && shift <= 28) {
        const uint8_t byte = data[offset++];
        value |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return value;
        }
        shift += 7;
    }
    ok = false;
    return 0;
}

bool skip_string(const std::vector<uint8_t>& data, size_t& offset) {
    bool ok = true;
    const uint32_t length = read_varint(data, offset, ok);
    if (!ok || offset + length > data.size()) {
        return false;
    }
    offset += length;
    return true;
}

/* Skip the command array and return the offset where it starts plus its count. */
bool locate_commands(const std::vector<uint8_t>& data, size_t& array_start, uint32_t& count) {
    size_t offset = 0;
    bool ok = true;

    const uint32_t values_len = read_varint(data, offset, ok);
    for (uint32_t i = 0; ok && i < values_len; ++i) {
        if (!skip_string(data, offset)) return false;
    }
    /* chained_subcommand_values */
    const uint32_t chained_values = read_varint(data, offset, ok);
    for (uint32_t i = 0; ok && i < chained_values; ++i) {
        if (!skip_string(data, offset)) return false;
    }
    /* suffixes */
    const uint32_t suffixes = read_varint(data, offset, ok);
    for (uint32_t i = 0; ok && i < suffixes; ++i) {
        if (!skip_string(data, offset)) return false;
    }
    /* enums: name + varint count + lu32 array */
    const uint32_t enums = read_varint(data, offset, ok);
    for (uint32_t i = 0; ok && i < enums; ++i) {
        if (!skip_string(data, offset)) return false;
        const uint32_t n = read_varint(data, offset, ok);
        if (!ok || offset + n * 4 > data.size()) return false;
        offset += n * 4;
    }
    /* chained_subcommands: name + count + (varint index, varint value) */
    const uint32_t chained = read_varint(data, offset, ok);
    for (uint32_t i = 0; ok && i < chained; ++i) {
        if (!skip_string(data, offset)) return false;
        const uint32_t n = read_varint(data, offset, ok);
        for (uint32_t j = 0; ok && j < n; ++j) {
            size_t ignored = offset;
            read_varint(data, ignored, ok);
            read_varint(data, ignored, ok);
            if (ok) offset = ignored;
        }
    }

    array_start = offset;
    count = read_varint(data, offset, ok);
    return ok;
}

/*
 * Commands are variable length, so the command array is walked field by field:
 * name, description, u16 flags, u8 permission, li32 alias enum, then overloads.
 */
bool skip_command_entry(const std::vector<uint8_t>& data, size_t& offset) {
    if (!skip_string(data, offset) || !skip_string(data, offset)) {
        return false;
    }
    if (offset + 2 + 1 + 4 > data.size()) {
        return false;
    }
    offset += 2; /* flags */
    offset += 1; /* permission level */
    offset += 4; /* alias enum index */

    bool ok = true;
    const uint32_t overloads = read_varint(data, offset, ok);
    for (uint32_t i = 0; ok && i < overloads; ++i) {
        const uint32_t params = read_varint(data, offset, ok);
        for (uint32_t j = 0; ok && j < params; ++j) {
            if (!skip_string(data, offset)) return false;      /* parameter name */
            if (offset + 2 + 1 + 2 + 1 > data.size()) return false;
            offset += 2; /* param type */
            offset += 1; /* enum type */
            offset += 2; /* optional flag + ...  */
            offset += 1; /* options byte */
        }
    }
    return ok;
}

SplicedTable splice(const std::vector<uint8_t>& payload) {
    SplicedTable out;
    size_t array_start = 0;
    uint32_t count = 0;
    if (!locate_commands(payload, array_start, count)) {
        return out;
    }
    /* prefix includes everything up to and including the command count varint */
    size_t cursor = 0;
    {
        /* re-walk to find the byte offset of the count varint */
        size_t offset = array_start;
        bool ok = true;
        read_varint(payload, offset, ok);
        if (!ok) {
            return out;
        }
        cursor = array_start;
        size_t after_count = array_start;
        bool ok2 = true;
        read_varint(payload, after_count, ok2);
        if (!ok2) {
            return out;
        }
        out.prefix.assign(payload.begin(), payload.begin() + cursor);
        /* command blob starts after the count varint */
        const size_t blob_start = after_count;
        size_t end = blob_start;
        for (uint32_t i = 0; i < count; ++i) {
            if (!skip_command_entry(payload, end)) {
                return {};
            }
        }
        out.command_blob.assign(payload.begin() + blob_start, payload.begin() + end);
        out.suffix.assign(payload.begin() + end, payload.end());
        out.command_count = count;
        out.ok = true;
    }
    return out;
}

/* Build one command entry with no parameters. */
void write_plugin_command(std::vector<uint8_t>& out, const ClientCommand& command) {
    put_string(out, command.name);
    put_string(out, command.description);
    /* flags: 0x00 (not hidden, no enum, no aliases) */
    put_u16(out, 0x0000);
    /* permission level */
    out.push_back(static_cast<uint8_t>(command.permission > 0 ? 1 : 0));
    /* alias enum index (-1 = none) */
    put_u32(out, 0xFFFFFFFFu);
    /* overloads: one overload with no parameters */
    put_varint(out, 1);
    put_varint(out, 0);
}

bool send_available_commands(void* player, const std::vector<uint8_t>& body) {
    return live_send_raw_packet(player, kPacketAvailableCommands, body);
}

} // namespace

void client_commands_capture(const void* packet) {
    if (packet == nullptr) {
        return;
    }
    const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
    /* Capture a generous slice; the packet is small (a few KB at most). */
    constexpr size_t kMaxCapture = 128 * 1024;
    std::lock_guard lock(command_mutex());
    auto& table = captured();
    table.payload.assign(payload, payload + kMaxCapture);
    table.valid = true;
    log_write(LogLevel::Info, "captured vanilla command table (%zu bytes)", table.payload.size());
}

void client_commands_set(const std::vector<ClientCommand>& commands) {
    std::lock_guard lock(command_mutex());
    plugin_commands() = commands;
}

bool client_commands_ready() {
    /*
     * Manual packet construction is not safe for this packet: AvailableCommands
     * carries std::vector members inside the packet object, and copying our
     * bytes over the payload destroys them.  Stay disabled until plugin commands
     * can be registered through CommandRegistry directly.
     */
    return false;
}

bool client_commands_send(void* player) {
    if (!client_commands_ready()) {
        return false;
    }
    if (player == nullptr) {
        return false;
    }

    /*
     * Build the table from scratch.
     *
     * The server sends its own AvailableCommandsPacket during login, before the
     * per-player send hook exists, so that payload cannot be captured.  A
     * self-contained table is enough for the client to accept plugin commands:
     * it only needs the sections to be present, not to mirror vanilla.
     */
    std::vector<ClientCommand> commands;
    {
        std::lock_guard lock(command_mutex());
        commands = plugin_commands();
    }

    std::vector<uint8_t> body;
    put_varint(body, 0); /* enum_values */
    put_varint(body, 0); /* chained_subcommand_values */
    put_varint(body, 0); /* suffixes */
    put_varint(body, 0); /* enums */
    put_varint(body, 0); /* chained_subcommands */

    put_varint(body, static_cast<uint32_t>(commands.size()));
    for (const auto& command : commands) {
        write_plugin_command(body, command);
    }

    put_varint(body, 0); /* dynamic_enums */
    put_varint(body, 0); /* enum_constraints */

    const bool sent = send_available_commands(player, body);
    log_write(LogLevel::Info, "sent command table to player: %zu command(s), %zu bytes, sent=%d",
              commands.size(), body.size(), sent ? 1 : 0);
    return sent;
}

void client_commands_broadcast() {
    for (const auto& info : players_online()) {
        if (info.handle != nullptr) {
            client_commands_send(info.handle);
        }
    }
}

void client_commands_refresh() {
    /*
     * Pull the current plugin command set out of the JS command registry and
     * publish it.  The registry owns the authoritative list; this module only
     * knows how to advertise it.
     */
    std::vector<ClientCommand> advertised;
    for (const auto& entry : js::js_registered_commands()) {
        ClientCommand command;
        command.name = entry.name;
        command.description = entry.description;
        command.permission = entry.permission;
        advertised.push_back(std::move(command));
    }
    client_commands_set(advertised);
    client_commands_broadcast();
}

void client_commands_clear() {
    std::lock_guard lock(command_mutex());
    plugin_commands().clear();
    captured().payload.clear();
    captured().valid = false;
}

} // namespace yoyo
