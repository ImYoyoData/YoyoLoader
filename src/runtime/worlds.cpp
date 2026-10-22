#include "worlds.h"

#include "live.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#endif

namespace yoyo::worlds {
namespace {

struct Entry {
    std::string name;
    int port = 0;
    std::string level;
};

struct Child {
#ifdef _WIN32
    PROCESS_INFORMATION process{};
    HANDLE stdin_write = nullptr;
#endif
    bool loaded = false;
};

std::mutex g_mutex;
std::vector<Entry> g_entries;
std::unordered_map<std::string, Child> g_children;
std::string g_instance_name;
bool g_controller = true;
int g_local_port = 0;
std::string g_local_level = "world";

std::filesystem::path root_dir() {
    return std::filesystem::current_path();
}

std::string trim(std::string value) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

void read_properties() {
    std::ifstream file(root_dir() / "server.properties");
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("server-port=", 0) == 0) {
            g_local_port = std::atoi(line.c_str() + std::strlen("server-port="));
        } else if (line.rfind("level-name=", 0) == 0) {
            g_local_level = line.substr(std::strlen("level-name="));
        }
    }
}

void load_config() {
    g_entries.clear();
    std::ifstream file(root_dir() / "yoyo_worlds.ini");
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::vector<std::string> parts;
        std::stringstream stream(line);
        std::string part;
        while (std::getline(stream, part, '|')) {
            parts.push_back(trim(part));
        }
        if (parts.size() < 2) {
            continue;
        }
        Entry entry;
        entry.name = parts[0];
        entry.port = std::atoi(parts[1].c_str());
        entry.level = parts.size() > 2 ? parts[2] : entry.name;
        if (!entry.name.empty() && entry.port > 0) {
            g_entries.push_back(entry);
        }
    }
    if (g_entries.empty()) {
        g_entries.push_back({"main", g_local_port, g_local_level});
    }
}

bool is_current(const Entry& entry) {
    return entry.name == g_instance_name;
}

void write_properties(const std::filesystem::path& source, const std::filesystem::path& destination,
                      const Entry& entry) {
    std::ifstream input(source / "server.properties");
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("server-name=", 0) == 0 || line.rfind("server-port=", 0) == 0 ||
            line.rfind("server-portv6=", 0) == 0 || line.rfind("level-name=", 0) == 0 ||
            line.rfind("enable-lan-visibility=", 0) == 0 || line.rfind("allow-list=", 0) == 0 ||
            line.rfind("online-mode=", 0) == 0 || line.rfind("default-player-permission-level=", 0) == 0 ||
            line.rfind("player-idle-timeout=", 0) == 0) {
            continue;
        }
        lines.push_back(line);
    }
    lines.push_back("server-name=YoyoLoader World " + entry.name);
    lines.push_back("server-port=" + std::to_string(entry.port));
    lines.push_back("server-portv6=" + std::to_string(entry.port + 1));
    lines.push_back("level-name=" + entry.level);
    lines.push_back("enable-lan-visibility=false");
    lines.push_back("allow-list=false");
    lines.push_back("online-mode=false");
    lines.push_back("default-player-permission-level=operator");
    lines.push_back("player-idle-timeout=0");

    std::ofstream output(destination / "server.properties", std::ios::trunc);
    for (const auto& item : lines) {
        output << item << '\n';
    }
}

void clone_tree(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::create_directories(destination, error);
    for (const auto& item : std::filesystem::directory_iterator(source, error)) {
        if (error) {
            break;
        }
        const auto name = item.path().filename().string();
        if (name == "worlds" || name == "instances" || name == "logs" || name == ".git" ||
            name == "server.properties" || name == "allowlist.json" || name == "permissions.json" ||
            name == "yoyo.log" || name == "server.out" || name == "yoyo_world_child.txt") {
            continue;
        }
        const auto target = destination / item.path().filename();
        if (item.is_directory()) {
            clone_tree(item.path(), target);
            continue;
        }
        std::filesystem::create_hard_link(item.path(), target, error);
        if (error) {
            error.clear();
            std::filesystem::copy_file(item.path(), target,
                                       std::filesystem::copy_options::overwrite_existing, error);
        }
    }
    std::filesystem::create_directories(destination / "worlds", error);
}

#ifdef _WIN32
bool wait_for_port(int port, int timeout_ms) {
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_handle != INVALID_SOCKET) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<unsigned short>(port));
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            const bool connected =
                connect(socket_handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
            closesocket(socket_handle);
            if (connected) {
                return true;
            }
        }
        Sleep(250);
    }
    WSACleanup();
    return false;
}

bool start_child(const Entry& entry) {
    const auto instance = root_dir() / "instances" / entry.name;
    const auto exe = instance / "yoyo.exe";
    std::error_code error;
    std::filesystem::create_directories(instance, error);
    clone_tree(root_dir(), instance);
    write_properties(root_dir(), instance, entry);
    std::ofstream(instance / "allowlist.json") << "[]\n";
    std::ofstream(instance / "permissions.json") << "[]\n";
    std::ofstream(instance / "yoyo_world_child.txt") << entry.name << '\n';

    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &attributes, 0)) {
        log_write(LogLevel::Error, "world '%s': CreatePipe failed", entry.name.c_str());
        return false;
    }
    SetHandleInformation(write_pipe, HANDLE_FLAG_INHERIT, 0);

    SECURITY_ATTRIBUTES null_attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE null_output = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     &null_attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = read_pipe;
    startup.hStdOutput = null_output;
    startup.hStdError = null_output;

    std::wstring command_line = exe.wstring();
    std::vector<wchar_t> command_buffer(command_line.begin(), command_line.end());
    command_buffer.push_back(L'\0');

    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        exe.c_str(), command_buffer.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, instance.c_str(), &startup, &process);
    CloseHandle(read_pipe);
    if (null_output != nullptr) {
        CloseHandle(null_output);
    }
    if (!created) {
        CloseHandle(write_pipe);
        log_write(LogLevel::Error, "world '%s': CreateProcess failed (%lu)", entry.name.c_str(), GetLastError());
        return false;
    }

    if (!wait_for_port(entry.port, 45000)) {
        log_write(LogLevel::Error, "world '%s': port %d did not become ready", entry.name.c_str(), entry.port);
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(write_pipe);
        return false;
    }

    Child child;
    child.process = process;
    child.stdin_write = write_pipe;
    child.loaded = true;
    g_children[entry.name] = child;
    log_write(LogLevel::Info, "world '%s' started on port %d", entry.name.c_str(), entry.port);
    return true;
}

void stop_child(const std::string& name) {
    auto it = g_children.find(name);
    if (it == g_children.end()) {
        return;
    }
    auto& child = it->second;
    if (child.stdin_write != nullptr) {
        const char stop[] = "stop\n";
        DWORD written = 0;
        WriteFile(child.stdin_write, stop, sizeof(stop) - 1, &written, nullptr);
        FlushFileBuffers(child.stdin_write);
    }
    if (child.process.hProcess != nullptr) {
        if (WaitForSingleObject(child.process.hProcess, 15000) == WAIT_TIMEOUT) {
            TerminateProcess(child.process.hProcess, 1);
            WaitForSingleObject(child.process.hProcess, 5000);
        }
        CloseHandle(child.process.hThread);
        CloseHandle(child.process.hProcess);
    }
    if (child.stdin_write != nullptr) {
        CloseHandle(child.stdin_write);
    }
    g_children.erase(it);
    log_write(LogLevel::Info, "world '%s' stopped", name.c_str());
}
#else
bool start_child(const Entry&) {
    return false;
}
void stop_child(const std::string&) {}
#endif

Entry* find_entry(const std::string& name) {
    for (auto& entry : g_entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

void initialize() {
    std::lock_guard lock(g_mutex);
    read_properties();
    g_instance_name = "main";
    std::ifstream marker(root_dir() / "yoyo_world_child.txt");
    std::string marker_name;
    if (std::getline(marker, marker_name)) {
        marker_name = trim(marker_name);
        if (!marker_name.empty()) {
            g_instance_name = marker_name;
            g_controller = false;
        }
    }
    load_config();
    if (g_local_port == 0) {
        g_local_port = 19132;
    }
    log_write(LogLevel::Info, "world manager: instance=%s controller=%d port=%d", g_instance_name.c_str(),
              g_controller ? 1 : 0, g_local_port);
}

std::vector<WorldInfo> list() {
    std::lock_guard lock(g_mutex);
    std::vector<WorldInfo> result;
    for (const auto& entry : g_entries) {
        WorldInfo info;
        info.name = entry.name;
        info.port = entry.port;
        info.level = entry.level;
        info.current = entry.name == g_instance_name;
        info.controller = g_controller;
        info.loaded = info.current;
        auto it = g_children.find(entry.name);
        if (it != g_children.end() && it->second.loaded) {
            info.loaded = true;
        }
        result.push_back(info);
    }
    return result;
}

std::string current_world() {
    std::lock_guard lock(g_mutex);
    return g_instance_name;
}

bool is_controller() {
    std::lock_guard lock(g_mutex);
    return g_controller;
}

bool load(const std::string& name) {
    std::lock_guard lock(g_mutex);
    auto* entry = find_entry(name);
    if (entry == nullptr || !g_controller) {
        return false;
    }
    if (name == g_instance_name) {
        return true;
    }
    auto it = g_children.find(name);
    if (it != g_children.end() && it->second.loaded) {
        return true;
    }
#ifdef _WIN32
    return start_child(*entry);
#else
    (void)entry;
    return false;
#endif
}

bool unload(const std::string& name) {
    std::lock_guard lock(g_mutex);
    if (!g_controller || name == g_instance_name) {
        return false;
    }
#ifdef _WIN32
    stop_child(name);
    return true;
#else
    (void)name;
    return false;
#endif
}

bool switch_player(void* player, const std::string& name) {
    Entry entry;
    {
        std::lock_guard lock(g_mutex);
        auto* found = find_entry(name);
        if (found == nullptr) {
            return false;
        }
        entry = *found;
        if (entry.name == g_instance_name) {
            return true;
        }
        if (!g_controller) {
            auto it = g_children.find(name);
            if (it == g_children.end() || !it->second.loaded) {
                /* A child can still transfer back to an already running world. */
                if (name != "main") {
                    return false;
                }
            }
        }
    }
    if (g_controller) {
        if (!load(name)) {
            return false;
        }
    }
    return live_transfer(player, "127.0.0.1", entry.port);
}

bool is_op(const std::string& xuid) {
    if (xuid.empty()) {
        return false;
    }
    std::ifstream file(root_dir() / "yoyo_ops.txt");
    std::string line;
    while (std::getline(file, line)) {
        if (trim(line) == xuid) {
            return true;
        }
    }
    return false;
}

} // namespace yoyo::worlds
