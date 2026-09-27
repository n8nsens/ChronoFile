#pragma once
#include <string>
#include <windows.h>

class Storage;

class Monitor {
public:
    Monitor(const std::wstring& watchPath, Storage* storage);
    ~Monitor();

    void Start();
    void Stop();

private:
    std::wstring m_watchPath;
    Storage* m_storage;
    HANDLE m_hDir;
    void* m_buffer;
    OVERLAPPED m_overlapped;
    volatile bool m_running;
    static constexpr DWORD BUFFER_SIZE = 64 * 1024;
};