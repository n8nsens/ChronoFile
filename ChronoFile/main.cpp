#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <thread>
#include "Storage.h"
#include "Monitor.h"

volatile bool g_running = true;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    _setmode(_fileno(stdout), _O_U16TEXT);

    const wchar_t* watchPath = (argc > 1) ? argv[1] : L"D:\\Kours_SP_3\\Test";

    Storage storage(L"logs\\chrono.log");
    Monitor monitor(watchPath, &storage);

    wprintf(L"ChronoFile started. Watching: %ls\n", watchPath);

    std::thread monitorThread([&monitor]() {
        monitor.Start();
        });

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    wprintf(L"\nShutting down...\n");
    monitor.Stop();
    monitorThread.join();

    wprintf(L"All resources released. Exit.\n");
    return 0;
}