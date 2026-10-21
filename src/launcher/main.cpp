#include "launcher.h"

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    return run_windows_launcher(argc, argv);
}
#else
int main(int argc, char** argv) {
    return run_linux_launcher(argc, argv);
}
#endif
