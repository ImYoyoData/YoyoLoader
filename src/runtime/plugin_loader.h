#pragma once

namespace yoyo {

/* Starts a worker thread that loads *.dll / *.so from <cwd>/plugins. */
void start_plugin_loader();

} // namespace yoyo
