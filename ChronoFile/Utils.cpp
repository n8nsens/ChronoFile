#include "Utils.h"
#include <algorithm>
#include <cwchar>

namespace Utils {
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

    std::string WStringToUtf8(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }
}