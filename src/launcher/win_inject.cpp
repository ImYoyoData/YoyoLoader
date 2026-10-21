#include "launcher.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <iterator>
#include <string>

namespace {

std::wstring executable_directory() {
    wchar_t buffer[32768];
    DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return {};
    }
    std::wstring path(buffer, length);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, slash);
}

bool is_file(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void print_error(const wchar_t* what) {
    DWORD error = GetLastError();
    std::fwprintf(stderr, L"[yoyo] %ls (error %lu)\n", what, error);
}

BOOL WINAPI ignore_ctrl_c(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        return TRUE; /* the child process handles it */
    }
    return FALSE;
}

} // namespace

int run_windows_launcher(int, wchar_t**) {
    std::wstring launcher_directory = executable_directory();
    if (launcher_directory.empty()) {
        std::fwprintf(stderr, L"[yoyo] cannot determine the launcher directory\n");
        return 1;
    }

    std::wstring directory;
    wchar_t current_buffer[32768] = {};
    DWORD current_length = GetCurrentDirectoryW(static_cast<DWORD>(std::size(current_buffer)), current_buffer);
    if (current_length > 0 && current_length < std::size(current_buffer)) {
        directory.assign(current_buffer, current_length);
    } else {
        directory = launcher_directory;
    }

    std::wstring server = directory + L"\\bedrock_server.exe";
    if (!is_file(server)) {
        directory = launcher_directory;
        server = directory + L"\\bedrock_server.exe";
    }
    std::wstring runtime = directory + L"\\yoyo_runtime.dll";

    if (!is_file(server)) {
        std::fwprintf(stderr, L"[yoyo] bedrock_server.exe not found in the current directory or next to yoyo.exe\n");
        return 1;
    }
    if (!is_file(runtime)) {
        std::fwprintf(stderr, L"[yoyo] yoyo_runtime.dll not found in %ls\n", directory.c_str());
        return 1;
    }

    std::wstring full_command = GetCommandLineW();
    size_t position = 0;
    if (!full_command.empty() && full_command[0] == L'"') {
        position = full_command.find(L'"', 1);
        if (position != std::wstring::npos) {
            ++position;
        } else {
            position = full_command.size();
        }
    } else {
        position = full_command.find_first_of(L" \t");
        if (position == std::wstring::npos) {
            position = full_command.size();
        }
    }
    std::wstring command_line = L"\"" + server + L"\"" + full_command.substr(position);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(server.c_str(), command_line.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr,
                        directory.c_str(), &startup, &process)) {
        print_error(L"CreateProcessW failed");
        return 1;
    }

    const size_t path_bytes = (runtime.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process.hProcess, nullptr, path_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_path == nullptr) {
        print_error(L"VirtualAllocEx failed");
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }

    if (!WriteProcessMemory(process.hProcess, remote_path, runtime.c_str(), path_bytes, nullptr)) {
        print_error(L"WriteProcessMemory failed");
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }

    auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")
    );
    if (load_library == nullptr) {
        std::fwprintf(stderr, L"[yoyo] cannot resolve LoadLibraryW\n");
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }

    HANDLE thread = CreateRemoteThread(process.hProcess, nullptr, 0, load_library, remote_path, 0, nullptr);
    if (thread == nullptr) {
        print_error(L"CreateRemoteThread failed");
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }

    bool injected = false;
    if (WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0) {
        DWORD module_handle = 0;
        if (GetExitCodeThread(thread, &module_handle) && module_handle != 0) {
            injected = true;
        }
    }
    CloseHandle(thread);

    if (!injected) {
        std::fwprintf(stderr, L"[yoyo] injection failed; terminating the suspended server\n");
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }

    SetConsoleCtrlHandler(ignore_ctrl_c, TRUE);
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}
