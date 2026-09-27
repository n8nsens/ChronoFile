#include "Storage.h"
#include "Utils.h"
#include "Hasher.h"

Storage::Storage(const std::wstring& logPath) : m_hLogFile(INVALID_HANDLE_VALUE) {
    InitLogFile(logPath);
}

Storage::~Storage() {
    CloseLogFile();
}

bool Storage::InitLogFile(const std::wstring& logPath) {
    CreateDirectoryW(L"logs", nullptr);
    bool fileExists = GetFileAttributesW(logPath.c_str()) != INVALID_FILE_ATTRIBUTES;

    m_hLogFile = CreateFileW(
        logPath.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (m_hLogFile != INVALID_HANDLE_VALUE && !fileExists) {
        const char bom[] = { (char)0xEF, (char)0xBB, (char)0xBF };
        DWORD bytesWritten;
        WriteFile(m_hLogFile, bom, 3, &bytesWritten, nullptr);
    }

    return m_hLogFile != INVALID_HANDLE_VALUE;
}

void Storage::CloseLogFile() {
    if (m_hLogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hLogFile);
        m_hLogFile = INVALID_HANDLE_VALUE;
    }
}

void Storage::WriteToLog(const std::wstring& msg) {
    if (m_hLogFile == INVALID_HANDLE_VALUE) return;
    std::string msgUtf8 = Utils::WStringToUtf8(msg);
    DWORD bytesWritten;
    WriteFile(m_hLogFile, msgUtf8.c_str(), (DWORD)msgUtf8.length(), &bytesWritten, nullptr);
}

void Storage::LogEvent(const wchar_t* action, const std::wstring& filename) {
    std::wstring msg = Utils::GetTimestamp() + L" " + action + L": " + filename + L"\r\n";
    WriteToLog(msg);
}

void Storage::AddPendingFile(const std::wstring& absPath) {
    m_pendingFiles[absPath] = std::chrono::steady_clock::now();
}

void Storage::RemovePendingFile(const std::wstring& absPath) {
    m_pendingFiles.erase(absPath);
}

void Storage::ProcessPendingFiles() {
    auto now = std::chrono::steady_clock::now();

    for (auto it = m_pendingFiles.begin(); it != m_pendingFiles.end(); ) {
        if (now - it->second >= m_debounceDuration) {
            std::wstring absPath = it->first;
            std::wstring hash = Hasher::ComputeSHA256(absPath);

            if (!hash.empty()) {
                CreateDirectoryW(L"storage", nullptr);
                CreateDirectoryW(L"storage/hot", nullptr);
                std::wstring destPath = L"storage/hot\\" + hash + L".bin";

                if (CopyFileW(absPath.c_str(), destPath.c_str(), FALSE)) {
                    wprintf(L"[STORAGE] %ls -> %ls\n", absPath.c_str(), destPath.c_str());
                    LogEvent(L"SAVED", absPath);
                }
            }
            it = m_pendingFiles.erase(it);
        }
        else {
            ++it;
        }
    }
}