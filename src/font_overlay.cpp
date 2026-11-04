#define WIN32_LEAN_AND_MEAN
#include "font_overlay.h"
#include "asset_io.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "MinHook.h"
#include "lodepng.h"

namespace {
using namespace mod_assets;
constexpr unsigned atlasSize = 2048;
constexpr unsigned overlayTop = 270;
constexpr wchar_t const* assetPaths[] = {
    L"fonts\\DIN.ttf", L"xml\\din.xml", L"atlases\\din_0.png", L"fonts\\din_0.png"
};
std::array<std::wstring, 4> originals, replacements;
decltype(&CreateFileW) originalOpenW;
decltype(&CreateFileA) originalOpenA;

bool writeFile(const std::wstring& path, const Bytes& bytes) {
    if (readFile(path) == bytes) return true;
    const auto temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool complete = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                                    &written, nullptr) && written == bytes.size();
    CloseHandle(file);
    if (complete && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
    DeleteFileW(temporary.c_str());
    return complete && readFile(path) == bytes;
}

Bytes applyDelta(const Bytes& source, Reader commands) {
    Bytes result;
    while (commands.remaining) {
        std::uint32_t word = 0;
        if (!commands.integer(word)) return {};
        const size_t length = word & 0x7fffffff;
        if (!length || length > maxFileSize - result.size()) return {};
        const unsigned char* begin;
        if (word & 0x80000000) {
            begin = commands.take(length);
            if (!begin) return {};
        } else {
            std::uint32_t offset = 0;
            if (!commands.integer(offset) || offset > source.size() ||
                length > source.size() - offset) return {};
            begin = source.data() + offset;
        }
        result.insert(result.end(), begin, begin + length);
    }
    return result;
}

bool replace(std::string& text, const std::string& oldText, const std::string& newText) {
    const auto offset = text.find(oldText);
    if (offset == std::string::npos) return false;
    text.replace(offset, oldText.size(), newText);
    return true;
}

const wchar_t* redirected(LPCWSTR path, DWORD access, DWORD disposition) {
    if (!path || disposition != OPEN_EXISTING ||
        (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA))) return nullptr;
    // The vast majority of file opens are unrelated game assets.
    const wchar_t* filename = path;
    for (const wchar_t* c = path; *c; ++c)
        if (*c == L'/' || *c == L'\\') filename = c + 1;
    if (_wcsicmp(filename, L"DIN.ttf") && _wcsicmp(filename, L"din.xml") &&
        _wcsicmp(filename, L"din_0.png")) return nullptr;
    wchar_t full[MAX_PATH];
    const DWORD length = GetFullPathNameW(path, MAX_PATH, full, nullptr);
    if (!length || length >= MAX_PATH) return nullptr;
    for (size_t i = 0; i < originals.size(); ++i)
        if (!_wcsicmp(full, originals[i].c_str())) return replacements[i].c_str();
    return nullptr;
}

HANDLE WINAPI openW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                     DWORD disposition, DWORD flags, HANDLE templateFile) {
    auto overlay = redirected(path, access, disposition);
    return originalOpenW(overlay ? overlay : path, access, share, security, disposition, flags, templateFile);
}

HANDLE WINAPI openA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                     DWORD disposition, DWORD flags, HANDLE templateFile) {
    wchar_t wide[MAX_PATH];
    if (path && MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH)) {
        if (auto overlay = redirected(wide, access, disposition))
            return originalOpenW(overlay, access, share, security, disposition, flags, templateFile);
    }
    return originalOpenA(path, access, share, security, disposition, flags, templateFile);
}
} // namespace

bool prepareFontOverlay(HMODULE module) {
    wchar_t location[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, location, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    std::wstring directory(location, length);
    directory.resize(directory.find_last_of(L"\\/") + 1);
    for (size_t i = 0; i < originals.size(); ++i) originals[i] = directory + assetPaths[i];

    const auto recipe = readFile(directory + L"wilmzh-font.wzh");
    Reader reader{recipe.data(), recipe.size()};
    const auto magic = reader.take(4);
    if (!magic || std::memcmp(magic, "WZH1", 4)) return false;
    std::uint64_t expectedTtf, expectedXml, expectedPng;
    std::uint32_t count, deltaSize, xmlSize, pngSize;
    if (!reader.integer(expectedTtf) || !reader.integer(expectedXml) || !reader.integer(expectedPng) ||
        !reader.integer(count) || !reader.integer(deltaSize) || !reader.integer(xmlSize) ||
        !reader.integer(pngSize)) return false;
    const auto delta = reader.take(deltaSize);
    const auto additions = reader.take(xmlSize);
    const auto glyphPng = reader.take(pngSize);
    if (!delta || !additions || !glyphPng || reader.remaining) return false;

    const auto ttf = readFile(originals[0]);
    const auto xml = readFile(originals[1]);
    const auto atlas = readFile(originals[2]);
    if (hash(ttf) != expectedTtf || hash(xml) != expectedXml || hash(atlas) != expectedPng) return false;
    const auto font = applyDelta(ttf, {delta, deltaSize});
    if (font.empty()) return false;
    std::string definition(xml.begin(), xml.end());
    if (!replace(definition, "scaleW=\"512\"", "scaleW=\"2048\"") ||
        !replace(definition, "scaleH=\"256\"", "scaleH=\"2048\"") ||
        !replace(definition, "<chars count=\"95\">", "<chars count=\"" + std::to_string(count) + "\">") ||
        !replace(definition, "  </chars>", std::string(reinterpret_cast<const char*>(additions), xmlSize) + "  </chars>"))
        return false;

    unsigned width = 0, height = 0, glyphWidth = 0, glyphHeight = 0;
    Bytes pixels, glyphPixels;
    if (lodepng::decode(pixels, width, height, atlas) || width != 512 || height != 256 ||
        lodepng::decode(glyphPixels, glyphWidth, glyphHeight, glyphPng, pngSize) ||
        glyphWidth != atlasSize || glyphHeight != atlasSize - overlayTop) return false;
    Bytes combined(atlasSize * atlasSize * 4);
    for (unsigned y = 0; y < height; ++y)
        std::memcpy(combined.data() + y * atlasSize * 4, pixels.data() + y * width * 4, width * 4);
    std::memcpy(combined.data() + overlayTop * atlasSize * 4, glyphPixels.data(), glyphPixels.size());
    Bytes mergedAtlas;
    if (lodepng::encode(mergedAtlas, combined, atlasSize, atlasSize)) return false;

    const auto cache = directory + L".wilmzh-cache";
    if (!CreateDirectoryW(cache.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    replacements = {cache + L"\\DIN.ttf", cache + L"\\din.xml", cache + L"\\din_0.png", cache + L"\\din_0.png"};
    if (!writeFile(replacements[0], font) ||
        !writeFile(replacements[1], Bytes(definition.begin(), definition.end())) ||
        !writeFile(replacements[2], mergedAtlas)) return false;
    return MH_CreateHook(reinterpret_cast<void*>(CreateFileW), reinterpret_cast<void*>(openW),
                         reinterpret_cast<void**>(&originalOpenW)) == MH_OK &&
           MH_CreateHook(reinterpret_cast<void*>(CreateFileA), reinterpret_cast<void*>(openA),
                         reinterpret_cast<void**>(&originalOpenA)) == MH_OK;
}
