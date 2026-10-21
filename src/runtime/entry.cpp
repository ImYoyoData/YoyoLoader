#include "runtime.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        /*
         * The launcher injects this DLL while the server's main thread is
         * still suspended, so hook installation happens before the game
         * starts any of its own threads.
         */
        yoyo::runtime_start();
        break;
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    default:
        break;
    }
    return TRUE;
}
#else
__attribute__((constructor)) static void yoyo_runtime_entry() {
    yoyo::runtime_start();
}
#endif
