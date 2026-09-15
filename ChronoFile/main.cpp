#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <string>
#include <algorithm>
#include <cwchar>

constexpr DWORD BUFFER_SIZE = 64 * 1024;
const wchar_t* DEFAULT_WATCH_PATH = L"D:/Kours_SP_3/Test";
volatile bool g_running = true;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

std::wstring GetTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"[%04d-%02d-%02d %02d:%02d:%02d]",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring GetFileName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

bool ShouldIgnoreFile(const std::wstring& relativePath) {
    std::wstring lower = ToLower(GetFileName(relativePath));
    if (lower.size() >= 2 && lower[0] == L'~' && lower[1] == L'$') return true;
    if (lower == L"thumbs.db" || lower == L".ds_store" || lower == L"desktop.ini") return true;
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".tmp") == 0) return true;
    return false;
}

const wchar_t* ActionToString(DWORD action) {
    switch (action) {
    case FILE_ACTION_ADDED:            return L"ADDED";
    case FILE_ACTION_REMOVED:          return L"REMOVED";
    case FILE_ACTION_MODIFIED:         return L"MODIFIED";
    case FILE_ACTION_RENAMED_OLD_NAME: return L"RENAMED_OLD";
    case FILE_ACTION_RENAMED_NEW_NAME: return L"RENAMED_NEW";
    default:                           return L"UNKNOWN";
    }
}

void LogEvent(const wchar_t* action, const std::wstring& filename) {
    wprintf(L"%ls %ls: %ls\n", GetTimestamp().c_str(), action, filename.c_str());
    fflush(stdout);
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    _setmode(_fileno(stdout), _O_U16TEXT);

    const wchar_t* watchPath = (argc > 1) ? argv[1] : DEFAULT_WATCH_PATH;

    wprintf(L"=== ChronoFile - базовый мониторинг ===\n");
    wprintf(L"Отслеживаемая папка: %ls\nДля выхода нажмите Ctrl+C\n\n", watchPath);

    HANDLE hDir = CreateFileW(
        watchPath,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (hDir == INVALID_HANDLE_VALUE) {
        wprintf(L"ОШИБКА: не удалось открыть папку (код %lu).\n", GetLastError());
        return 1;
    }

    void* buffer = VirtualAlloc(nullptr, BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buffer) {
        wprintf(L"ОШИБКА: не удалось выделить буфер (код %lu).\n", GetLastError());
        CloseHandle(hDir);
        return 1;
    }

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        wprintf(L"ОШИБКА: не удалось создать событие (код %lu).\n", GetLastError());
        VirtualFree(buffer, 0, MEM_RELEASE);
        CloseHandle(hDir);
        return 1;
    }

    DWORD notifyFilter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
        FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_DIR_NAME;
    DWORD bytesReturned = 0;

    while (g_running) {
        BOOL result = ReadDirectoryChangesW(
            hDir, buffer, BUFFER_SIZE, TRUE, notifyFilter,
            &bytesReturned, &overlapped, nullptr
        );

        if (!result && GetLastError() != ERROR_IO_PENDING) {
            wprintf(L"ОШИБКА ReadDirectoryChangesW: %lu\n", GetLastError());
            break;
        }

        DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 500);

        if (waitResult == WAIT_TIMEOUT) {
            CancelIo(hDir);
            WaitForSingleObject(overlapped.hEvent, INFINITE);
            GetOverlappedResult(hDir, &overlapped, &bytesReturned, FALSE);
            continue;
        }

        if (waitResult != WAIT_OBJECT_0) {
            wprintf(L"ОШИБКА WaitForSingleObject: %lu\n", GetLastError());
            break;
        }

        if (!GetOverlappedResult(hDir, &overlapped, &bytesReturned, FALSE)) {
            DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED) continue;
            if (err == ERROR_NOTIFY_ENUM_DIR) {
                wprintf(L"[WARNING] Буфер переполнен! Пропущены события.\n");
                continue;
            }
            wprintf(L"ОШИБКА GetOverlappedResult: %lu\n", err);
            break;
        }

        if (bytesReturned == 0) continue;

        BYTE* pBase = static_cast<BYTE*>(buffer);
        DWORD offset = 0;

        do {
            FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(pBase + offset);
            DWORD charCount = info->FileNameLength / sizeof(WCHAR);
            std::wstring filename(info->FileName, charCount);

            if (!ShouldIgnoreFile(filename)) {
                LogEvent(ActionToString(info->Action), filename);
            }

            if (info->NextEntryOffset == 0) break;
            offset += info->NextEntryOffset;
        } while (true);
    }

    wprintf(L"\nЗавершение работы...\n");
    CancelIo(hDir);
    CloseHandle(overlapped.hEvent);
    VirtualFree(buffer, 0, MEM_RELEASE);
    CloseHandle(hDir);
    wprintf(L"Все ресурсы освобождены. Выход.\n");
    return 0;
}