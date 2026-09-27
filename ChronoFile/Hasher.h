#pragma once
#include <string>

namespace Hasher {
    std::wstring ComputeSHA256(const std::wstring& filePath);
}