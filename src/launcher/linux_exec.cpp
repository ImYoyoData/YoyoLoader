#include "launcher.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

std::string executable_path() {
    std::vector<char> buffer(4096);
    for (;;) {
        ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (length < 0) {
            return {};
        }
        if (static_cast<size_t>(length) < buffer.size() - 1) {
            buffer[static_cast<size_t>(length)] = '\0';
            return std::string(buffer.data(), static_cast<size_t>(length));
        }
        buffer.resize(buffer.size() * 2);
    }
}

} // namespace

int run_linux_launcher(int argc, char** argv) {
    std::string self = executable_path();
    if (self.empty()) {
        std::fprintf(stderr, "[yoyo] cannot determine the launcher path\n");
        return 1;
    }
    size_t slash = self.find_last_of('/');
    std::string launcher_directory = (slash == std::string::npos) ? "." : self.substr(0, slash);

    char current_buffer[4096] = {};
    std::string directory;
    if (getcwd(current_buffer, sizeof(current_buffer)) != nullptr) {
        directory = current_buffer;
    } else {
        directory = launcher_directory;
    }

    std::string server = directory + "/bedrock_server";
    if (access(server.c_str(), X_OK) != 0) {
        directory = launcher_directory;
        server = directory + "/bedrock_server";
    }
    std::string runtime = directory + "/libyoyo_runtime.so";

    if (access(server.c_str(), X_OK) != 0) {
        std::fprintf(stderr, "[yoyo] bedrock_server not found in the current directory or next to yoyo\n");
        return 1;
    }
    if (access(runtime.c_str(), R_OK) != 0) {
        std::fprintf(stderr, "[yoyo] libyoyo_runtime.so not found: %s\n", runtime.c_str());
        return 1;
    }

    if (chdir(directory.c_str()) != 0) {
        std::perror("[yoyo] chdir");
        return 1;
    }

    const char* existing = std::getenv("LD_PRELOAD");
    std::string preload = runtime;
    if (existing != nullptr && existing[0] != '\0') {
        preload += ':';
        preload += existing;
    }
    if (setenv("LD_PRELOAD", preload.c_str(), 1) != 0) {
        std::perror("[yoyo] setenv(LD_PRELOAD)");
        return 1;
    }

    std::vector<char*> args;
    args.reserve(static_cast<size_t>(argc) + 1);
    args.push_back(const_cast<char*>(server.c_str()));
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    args.push_back(nullptr);

    execv(server.c_str(), args.data());
    std::perror("[yoyo] execv");
    return 1;
}
