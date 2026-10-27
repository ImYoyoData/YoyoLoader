#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace yoyo {

/*
 * Client command table.
 *
 * BDS sends AvailableCommandsPacket (id 76) to each player after login; the
 * client builds its tab-completion list and /help output from it.  Injecting
 * our own entries into that packet is what makes a plugin command look like a
 * real command instead of an unknown one.
 *
 * The packet is rebuilt per player from:
 *   - the command set the server already sent (captured from the outgoing
 *     stream), so vanilla commands keep working, plus
 *   - the commands registered by plugins through mc.regPlayerCmd.
 *
 * Everything is re-sent when the plugin command set changes.
 */
struct ClientCommand {
    std::string name;
    std::string description;
    /* 0 = any player, > 0 = operators only. */
    int permission = 0;
    /* Optional literal aliases shown in the client's list. */
    std::vector<std::string> aliases;
};

/* Registers a command that should appear in the client command table. */
void client_commands_set(const std::vector<ClientCommand>& commands);

/*
 * True when a safe advertisement path exists for this build.  Currently always
 * false: writing an AvailableCommandsPacket by hand corrupts the packet's STL
 * members.  Kept so call sites stay ready for the CommandRegistry path.
 */
bool client_commands_ready();

/* Send the command table to one player (only valid when ready()). */
bool client_commands_send(void* player);

/* Called from the outgoing packet observer when the server itself sent an
 * AvailableCommandsPacket; keeps its payload for later merges. */
void client_commands_capture(const void* packet);

/* Re-send to everyone (used after registering new commands). */
void client_commands_broadcast();

/*
 * Rebuild the advertised command list from the plugin registry and push it to
 * every online player. Called whenever mc.regPlayerCmd registers something.
 */
void client_commands_refresh();

void client_commands_clear();

} // namespace yoyo
