#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <string>
#include <algorithm>
#include <cwchar>
#include <wincrypt.h>
#include <vector>
#include <map>
#include <chrono>

constexpr DWORD BUFFER_SIZE = 64 * 1024;
const wchar_t* DEFAULT_WATCH_PATH = L"D:\\Kours_SP_3\\Test";
volatile bool g_running = true;

// Глобальный хэндл лог-файла
HANDLE g_hLogFile = INVALID_HANDLE_VALUE;

// Словарь ожидающих обработки файлов: Ключ = Абсолютный путь, Значение = Время последнего изменения
std::map<std::wstring, std::chrono::steady_clock::time_point> g_pendingFiles;
const auto DEBOUNCE_DURATION = std::chrono::seconds(5); // Ждем 5 секунд тишины

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

// Инициализация лог-файла через WinAPI
// --- НОВАЯ ФУНКЦИЯ: Конвертация wstring (UTF-16) в string (UTF-8) ---
std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    // Узнаем требуемый размер буфера в байтах
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    // Выполняем конвертацию
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// --- ОБНОВЛЕННАЯ: Инициализация с записью BOM ---
bool InitLogFile(const std::wstring& logPath) {
    CreateDirectoryW(L"logs", nullptr);

    // Проверяем, существует ли файл, чтобы не писать BOM каждый раз при запуске
    bool fileExists = GetFileAttributesW(logPath.c_str()) != INVALID_FILE_ATTRIBUTES;

    g_hLogFile = CreateFileW(
        logPath.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    // Если файл создан только что, записываем UTF-8 BOM (EF BB BF)
    if (g_hLogFile != INVALID_HANDLE_VALUE && !fileExists) {
        const char bom[] = { (char)0xEF, (char)0xBB, (char)0xBF };
        DWORD bytesWritten;
        WriteFile(g_hLogFile, bom, 3, &bytesWritten, nullptr);
    }

    return g_hLogFile != INVALID_HANDLE_VALUE;
}

// --- ОБНОВЛЕННАЯ: Запись уже в UTF-8 ---
void LogToFile(const wchar_t* action, const std::wstring& filename) {
    if (g_hLogFile == INVALID_HANDLE_VALUE) return;

    // 1. Собираем строку как wstring
    std::wstring msgW = GetTimestamp() + L" " + action + L": " + filename + L"\r\n";

    // 2. Конвертируем в UTF-8
    std::string msgUtf8 = WStringToUtf8(msgW);

    // 3. Пишем в файл (теперь длина считается в обычных байтах UTF-8)
    DWORD bytesWritten;
    WriteFile(
        g_hLogFile,
        msgUtf8.c_str(),
        (DWORD)msgUtf8.length(),
        &bytesWritten,
        nullptr
    );
}

// Закрытие лог-файла
void CloseLogFile() {
    if (g_hLogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hLogFile);
        g_hLogFile = INVALID_HANDLE_VALUE;
    }
}

// Конвертация байтов хэша в hex-строку (нижний регистр)
std::wstring BytesToHex(const BYTE* bytes, DWORD length) {
    std::wstring hex;
    hex.reserve(length * 2);
    for (DWORD i = 0; i < length; i++) {
        wchar_t buf[3];
        swprintf_s(buf, L"%02x", bytes[i]);
        hex += buf;
    }
    return hex;
}

// Вычисление SHA-256 для файла (ДИАГНОСТИЧЕСКАЯ ВЕРСИЯ)
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
        wprintf(L"[DEBUG] CreateFileW не смог открыть файл. Ошибка: %lu\n", GetLastError());
        return L"";
    }

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::wstring result;

    // 1. Получаем контекст криптопровайдера
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        DWORD err = GetLastError();
        if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_NEWKEYSET)) {
            wprintf(L"[DEBUG] CryptAcquireContextW failed. Ошибка: %lu\n", err);
            CloseHandle(hFile);
            return L"";
        }
    }

    // 2. Создаём объект хэша SHA-256
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        wprintf(L"[DEBUG] CryptCreateHash failed. Ошибка: %lu\n", GetLastError());
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return L"";
    }

    // 3. Читаем файл чанками по 1 МБ
    const DWORD CHUNK_SIZE = 1024 * 1024; // 1 МБ
    std::vector<BYTE> buffer(CHUNK_SIZE);
    DWORD bytesRead = 0;

    bool hashSuccess = true;
    while (ReadFile(hFile, buffer.data(), CHUNK_SIZE, &bytesRead, nullptr) && bytesRead > 0) {
        if (!CryptHashData(hHash, buffer.data(), bytesRead, 0)) {
            wprintf(L"[DEBUG] CryptHashData failed. Ошибка: %lu\n", GetLastError());
            hashSuccess = false;
            break;
        }
    }

    // 4. Получаем итоговый хэш
    if (hashSuccess) {
        DWORD hashSize = 32; // SHA-256 = 32 байта
        BYTE hashBytes[32];
        if (CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashSize, 0)) {
            result = BytesToHex(hashBytes, hashSize);
        }
        else {
            wprintf(L"[DEBUG] CryptGetHashParam failed. Ошибка: %lu\n", GetLastError());
        }
    }

    // 5. Освобождаем ресурсы
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);

    return result;
}

// Проверяет файлы, которые "успокоились" (прошло 5 сек), хэширует их и сохраняет в storage/hot/
void ProcessPendingFiles(const std::wstring& watchPath) {
    auto now = std::chrono::steady_clock::now();

    for (auto it = g_pendingFiles.begin(); it != g_pendingFiles.end(); ) {
        // Если прошло больше 5 секунд с последнего изменения
        if (now - it->second >= DEBOUNCE_DURATION) {
            std::wstring absPath = it->first;

            // 1. Считаем хэш
            std::wstring hash = ComputeSHA256(absPath);

            if (!hash.empty()) {
                // 2. Создаем папки хранения, если их нет
                CreateDirectoryW(L"storage", nullptr);
                CreateDirectoryW(L"storage/hot", nullptr);

                // 3. Формируем путь: storage/hot/{хэш}.bin
                std::wstring destPath = L"storage/hot\\" + hash + L".bin";

                // 4. Копируем файл (FALSE = перезаписывать, если вдруг уже есть)
                if (CopyFileW(absPath.c_str(), destPath.c_str(), FALSE)) {
                    wprintf(L"[STORAGE] Сохранено: %ls -> %ls\n", absPath.c_str(), destPath.c_str());
                    LogToFile(L"SAVED", absPath);
                }
                else {
                    wprintf(L"[STORAGE] Ошибка копирования %ls (Код: %lu)\n", absPath.c_str(), GetLastError());
                }
            }
            // Удаляем файл из словаря ожидания
            it = g_pendingFiles.erase(it);
        }
        else {
            ++it;
        }
    }
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

    // --- ТЕСТ ХЭШИРОВАНИЯ ---
    wprintf(L"\n=== Тест хэширования ===\n");

    // Хешируем файл лога, который мы точно только что создали!
    std::wstring logHash = ComputeSHA256(L"logs\\chrono.log");
    wprintf(L"Файл chrono.log: %ls\n", logHash.c_str());

    // Или укажи полный путь к ЛЮБОМУ файлу, который ты точно видишь в папке
    // std::wstring testHash = ComputeSHA256(L"D:\\Kours_SP_3\\Test\\ТвойРеальныйФайл.txt");
    // wprintf(L"Твой файл: %ls\n", testHash.c_str());

    wprintf(L"======================\n\n");
    // --- КОНЕЦ ТЕСТА ---

    // 1. Инициализация лог-файла
    if (!InitLogFile(L"logs\\chrono.log")) {
        wprintf(L"ОШИБКА: не удалось открыть лог-файл\n");
        return 1;
    }

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

            // ПРОВЕРЯЕМ ОЖИДАЮЩИЕ ФАЙЛЫ КАЖДЫЕ 500 МС
            ProcessPendingFiles(watchPath);

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
                // Формируем абсолютный путь к файлу
                std::wstring absPath = std::wstring(watchPath) + L"\\" + filename;

                // Если файл добавлен или изменен — добавляем/обновляем его в словаре ожидания
                if (info->Action == FILE_ACTION_ADDED || info->Action == FILE_ACTION_MODIFIED) {
                    g_pendingFiles[absPath] = std::chrono::steady_clock::now();
                    LogEvent(ActionToString(info->Action), filename);
                    LogToFile(ActionToString(info->Action), filename);
                }
                // Если удален — удаляем из словаря, чтобы не хэшировать несуществующий файл
                else if (info->Action == FILE_ACTION_REMOVED) {
                    g_pendingFiles.erase(absPath);
                    LogEvent(ActionToString(info->Action), filename);
                    LogToFile(ActionToString(info->Action), filename);
                }
                else {
                    LogEvent(ActionToString(info->Action), filename);
                    LogToFile(ActionToString(info->Action), filename);
                }
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
    CloseLogFile(); // Закрываем лог перед выходом
    wprintf(L"Все ресурсы освобождены. Выход.\n");
    return 0;
}