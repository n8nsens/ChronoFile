#pragma once
#include <string>
#include <map>
#include <chrono>
#include <windows.h>

class Storage {
public:
    Storage(const std::wstring& logPath);
    ~Storage();

    void AddPendingFile(const std::wstring& absPath);
    void RemovePendingFile(const std::wstring& absPath);
    void ProcessPendingFiles();
    void LogEvent(const wchar_t* action, const std::wstring& filename);

private:
    HANDLE m_hLogFile;
    std::map<std::wstring, std::chrono::steady_clock::time_point> m_pendingFiles;
    const std::chrono::seconds m_debounceDuration{ 5 };

    bool InitLogFile(const std::wstring& logPath);
    void CloseLogFile();
    void WriteToLog(const std::wstring& msg);
};