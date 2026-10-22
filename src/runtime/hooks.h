#pragma once

namespace yoyo {

/* Installs the inline hooks that feed the event bus. */
bool install_player_hooks();

/* Trampoline of the original disconnectClientWithMessage. */
void* disconnect_original_fn();

} // namespace yoyo
