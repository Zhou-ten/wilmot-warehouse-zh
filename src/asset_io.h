#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mod_assets {
using Bytes = std::vector<unsigned char>;
constexpr size_t maxFileSize = 16 * 1024 * 1024;

inline Bytes readFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    Bytes data;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= maxFileSize) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) ||
            read != data.size()) data.clear();
    }
    CloseHandle(file);
    return data;
}

inline std::uint64_t hash(const Bytes& bytes) {
    std::uint64_t result = 14695981039346656037ULL;
    for (auto byte : bytes) result = (result ^ byte) * 1099511628211ULL;
    return result;
}

struct Reader {
    const unsigned char* data;
    size_t remaining;
    const unsigned char* take(size_t size) {
        if (size > remaining) return nullptr;
        const auto begin = data;
        data += size;
        remaining -= size;
        return begin;
    }
    template<class T> bool integer(T& value) {
        const auto field = take(sizeof(T));
        if (!field) return false;
        std::memcpy(&value, field, sizeof(T));
        return true;
    }
};
} // namespace mod_assets
