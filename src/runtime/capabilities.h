#pragma once

#include <string>

#include "version.h"

namespace yoyo {

/*
 * Capability queries.
 *
 * Every runtime path that depends on a hook, an object layout or a packet
 * field offset must ask here first.  A false answer means the feature is not
 * verified for the running build: the call site must fail cleanly instead of
 * touching memory whose layout was never checked.
 *
 * See PROJECT_RULES.md 1.4.
 */
const VersionCapabilities& capabilities();

bool capability_enabled(bool VersionCapabilities::*flag);

/* Prints the version, platform and the list of disabled features. */
void log_capability_report();

/* Comma-separated list of disabled features, or "full". */
std::string capability_summary();

} // namespace yoyo
