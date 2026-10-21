#pragma once

#include <cstdarg>

namespace yoyo {

enum class LogLevel : int { Info = 0, Warn = 1, Error = 2 };

void log_open();
void log_close();
void log_write(LogLevel level, const char* fmt, ...);
void log_vwrite(LogLevel level, const char* fmt, va_list args);

} // namespace yoyo
