#include "Hasher.h"
#include <windows.h>
#include <wincrypt.h>
#include <vector>

namespace Hasher {
    static std::wstring BytesToHex(const BYTE* bytes, DWORD length) {
        std::wstring hex;
        hex.reserve(length * 2);
        for (DWORD i = 0; i < length; i++) {
            wchar_t buf[3];
            swprintf_s(buf, L"%02x", bytes[i]);
            hex += buf;
        }
        return hex;
    }

    std::wstring ComputeSHA256(const std::wstring& filePath) {
        HANDLE hFile = CreateFileW(
            filePath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (hFile == INVALID_HANDLE_VALUE) {
            return L""; 
        }

        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        std::wstring result;

        if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_NEWKEYSET)) {
                CloseHandle(hFile);
                return L"";
            }
        }

        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            CryptReleaseContext(hProv, 0);
            CloseHandle(hFile);
            return L"";
        }

        const DWORD CHUNK_SIZE = 1024 * 1024; // 1 МБ
        std::vector<BYTE> buffer(CHUNK_SIZE);
        DWORD bytesRead = 0;
        bool hashSuccess = true;

        while (ReadFile(hFile, buffer.data(), CHUNK_SIZE, &bytesRead, nullptr) && bytesRead > 0) {
            if (!CryptHashData(hHash, buffer.data(), bytesRead, 0)) {
                hashSuccess = false;
                break;
            }
        }

        if (hashSuccess) {
            DWORD hashSize = 32;
            BYTE hashBytes[32];
            if (CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashSize, 0)) {
                result = BytesToHex(hashBytes, hashSize);
            }
        }

        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);

        return result;
    }
}