#include "Monitor.h"
#include "Storage.h"
#include "Utils.h"

Monitor::Monitor(const std::wstring& watchPath, Storage* storage)
    : m_watchPath(watchPath), m_storage(storage), m_hDir(INVALID_HANDLE_VALUE), m_buffer(nullptr), m_running(false) {
    m_overlapped = {};
}

Monitor::~Monitor() {
    Stop();
}

void Monitor::Start() {
    m_running = true;

    m_hDir = CreateFileW(
        m_watchPath.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (m_hDir == INVALID_HANDLE_VALUE) {
        wprintf(L"Monitor: cannot open %ls\n", m_watchPath.c_str());
        return;
    }

    m_buffer = VirtualAlloc(nullptr, BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!m_buffer) {
        CloseHandle(m_hDir);
        return;
    }

    m_overlapped.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_overlapped.hEvent) {
        VirtualFree(m_buffer, 0, MEM_RELEASE);
        CloseHandle(m_hDir);
        return;
    }

    DWORD notifyFilter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
        FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_DIR_NAME;
    DWORD bytesReturned = 0;

    while (m_running) {
        BOOL result = ReadDirectoryChangesW(
            m_hDir, m_buffer, BUFFER_SIZE, TRUE, notifyFilter,
            &bytesReturned, &m_overlapped, nullptr
        );

        if (!result && GetLastError() != ERROR_IO_PENDING) {
            break;
        }

        DWORD waitResult = WaitForSingleObject(m_overlapped.hEvent, 500);

        if (waitResult == WAIT_TIMEOUT) {
            CancelIo(m_hDir);
            WaitForSingleObject(m_overlapped.hEvent, INFINITE);
            GetOverlappedResult(m_hDir, &m_overlapped, &bytesReturned, FALSE);
            if (m_storage) m_storage->ProcessPendingFiles();
            continue;
        }

        if (waitResult != WAIT_OBJECT_0) break;

        if (!GetOverlappedResult(m_hDir, &m_overlapped, &bytesReturned, FALSE)) {
            DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED) continue;
            if (err == ERROR_NOTIFY_ENUM_DIR) {
                wprintf(L"[WARNING] Buffer overflow\n");
                continue;
            }
            break;
        }

        if (bytesReturned == 0) continue;

        BYTE* pBase = static_cast<BYTE*>(m_buffer);
        DWORD offset = 0;

        do {
            FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(pBase + offset);
            DWORD charCount = info->FileNameLength / sizeof(WCHAR);
            std::wstring filename(info->FileName, charCount);
            std::wstring absPath = m_watchPath + L"\\" + filename;

            DWORD attrs = GetFileAttributesW(absPath.c_str());
            bool isDirectory = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));

            if (!isDirectory && !Utils::ShouldIgnoreFile(filename)) {
                wprintf(L"%ls %ls\n", Utils::ActionToString(info->Action), filename.c_str());
                m_storage->LogEvent(Utils::ActionToString(info->Action), filename);

                if (info->Action == FILE_ACTION_ADDED ||
                    info->Action == FILE_ACTION_MODIFIED ||
                    info->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                    m_storage->AddPendingFile(absPath);
                }
                else if (info->Action == FILE_ACTION_REMOVED ||
                    info->Action == FILE_ACTION_RENAMED_OLD_NAME) {
                    m_storage->RemovePendingFile(absPath);
                }
            }

            if (info->NextEntryOffset == 0) break;
            offset += info->NextEntryOffset;
        } while (true);
    }
}

void Monitor::Stop() {
    m_running = false;
    if (m_overlapped.hEvent) {
        CancelIo(m_hDir);
        CloseHandle(m_overlapped.hEvent);
        m_overlapped.hEvent = nullptr;
    }
    if (m_buffer) {
        VirtualFree(m_buffer, 0, MEM_RELEASE);
        m_buffer = nullptr;
    }
    if (m_hDir != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDir);
        m_hDir = INVALID_HANDLE_VALUE;
    }
}