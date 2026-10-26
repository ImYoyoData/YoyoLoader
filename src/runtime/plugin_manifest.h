#pragma once

#include <string>
#include <vector>

namespace yoyo::plugins {

/*
 * Plugin manifest.
 *
 * Every plugin is a directory under <server>/plugins containing a
 * manifest.json:
 *
 *   {
 *     "name": "home",
 *     "version": "1.0.0",
 *     "engine": "javascript",          // javascript | native
 *     "entry": "main.js",              // or "home.dll" / "home.so"
 *     "dependencies": ["core"],        // required, load order enforced
 *     "optionalDependencies": ["ui"],  // loaded first when present, never required
 *     "description": "waypoint homes"
 *   }
 *
 * A loose .js file in plugins/ is a convenience: the loader creates
 * plugins/<stem>/ containing the file and a generated manifest, so JS plugins
 * never have to be hand-packaged.  Native (C++/Go) plugins must be directories,
 * because a shared library alone carries no manifest.
 */
enum class Engine {
    Javascript,
    Native,
};

struct Manifest {
    std::string name;
    std::string version = "0.0.0";
    std::string description;
    Engine engine = Engine::Javascript;
    std::string entry;
    std::vector<std::string> dependencies;
    std::vector<std::string> optional_dependencies;
    /* Directory that holds the plugin. */
    std::string directory;
    /* Set when the manifest was generated for a loose .js file. */
    bool generated = false;
};

/* Human-readable engine name for logs and docs. */
const char* engine_name(Engine engine);

/*
 * Scan <server>/plugins, build the manifest list, and return it in load order.
 *
 * Behaviour:
 *   - a loose .js file is moved into its own directory with a generated
 *     manifest (once; subsequent runs reuse it);
 *   - a directory without manifest.json is reported and skipped;
 *   - a required dependency that is missing disables the dependent plugin;
 *   - a dependency cycle is detected and every plugin in the cycle is disabled;
 *   - optional dependencies only affect ordering when present.
 *
 * `skipped` receives one human-readable reason per plugin that will not load.
 */
std::vector<Manifest> discover(const std::string& plugins_directory, std::vector<std::string>& skipped);

} // namespace yoyo::plugins
