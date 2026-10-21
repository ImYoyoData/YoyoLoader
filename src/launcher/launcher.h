#pragma once

#ifdef _WIN32
int run_windows_launcher(int argc, wchar_t** argv);
#else
int run_linux_launcher(int argc, char** argv);
#endif
