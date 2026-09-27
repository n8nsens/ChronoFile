#pragma once
#include <string>
#include <windows.h>

namespace Utils {
    std::wstring GetTimestamp();
    std::wstring GetFileName(const std::wstring& path);
    std::wstring ToLower(std::wstring s);
    bool ShouldIgnoreFile(const std::wstring& relativePath);
    const wchar_t* ActionToString(DWORD action);
    std::string WStringToUtf8(const std::wstring& wstr);
}