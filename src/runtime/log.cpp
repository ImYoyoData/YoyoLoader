#include "log.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/stat.h>
#endif

namespace yoyo {
namespace {

std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
intptr_t g_file = -1;

class SpinLock {
public:
    SpinLock() {
        while (g_lock.test_and_set(std::memory_order_acquire)) {
        }
    }
    ~SpinLock() { g_lock.clear(std::memory_order_release); }
};

const char* level_name(LogLevel level) {
    switch (level) {
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    default: return "INFO";
    }
}

void write_bytes(const char* data, size_t size) {
    if (g_file == -1) {
        return;
    }
#ifdef _WIN32
    auto handle = reinterpret_cast<HANDLE>(g_file);
    while (size > 0) {
        DWORD written = 0;
        if (!WriteFile(handle, data, static_cast<DWORD>(size), &written, nullptr) || written == 0) {
            break;
        }
        data += written;
        size -= written;
    }
#else
    while (size > 0) {
        ssize_t written = ::write(static_cast<int>(g_file), data, size);
        if (written <= 0) {
            break;
        }
        data += written;
        size -= static_cast<size_t>(written);
    }
#endif
}

} // namespace

void log_open() {
    if (g_file != -1) {
        return;
    }
#ifdef _WIN32
    HANDLE handle = CreateFileA(
        "yoyo.log",
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (handle != INVALID_HANDLE_VALUE) {
        g_file = reinterpret_cast<intptr_t>(handle);
    }
#else
    int fd = ::open("yoyo.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd >= 0) {
        g_file = fd;
    }
#endif
}

void log_close() {
    if (g_file == -1) {
        return;
    }
#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(g_file));
#else
    ::close(static_cast<int>(g_file));
#endif
    g_file = -1;
}

void log_vwrite(LogLevel level, const char* fmt, va_list args) {
    char message[2048];
    int len = vsnprintf(message, sizeof(message), fmt, args);
    if (len < 0) {
        return;
    }
    if (static_cast<size_t>(len) >= sizeof(message)) {
        len = static_cast<int>(sizeof(message) - 1);
    }

    char line[2304];
    char stamp[32] = "";
#ifdef _WIN32
    SYSTEMTIME st{};
    GetLocalTime(&st);
    snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u %02u:%02u:%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
#else
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&now, &tmv);
    snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
#endif
    int total = snprintf(line, sizeof(line), "[%s] [%s] %.*s\n", stamp, level_name(level), len, message);
    if (total <= 0) {
        return;
    }
    if (static_cast<size_t>(total) >= sizeof(line)) {
        total = static_cast<int>(sizeof(line) - 1);
    }

    SpinLock lock;
    write_bytes(line, static_cast<size_t>(total));
}

void log_write(LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_vwrite(level, fmt, args);
    va_end(args);
}

} // namespace yoyo
