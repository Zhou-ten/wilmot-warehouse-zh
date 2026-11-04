#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
HMODULE systemVersion() {
    static HMODULE handle = [] {
        wchar_t path[MAX_PATH];
        const UINT size = GetSystemDirectoryW(path, MAX_PATH);
        if (!size || size >= MAX_PATH - 13) return HMODULE{};
        lstrcpyW(path + size, L"\\version.dll");
        return LoadLibraryW(path);
    }();
    return handle;
}

template<class Function>
Function systemExport(const char* name) {
    const auto module = systemVersion();
    return module ? reinterpret_cast<Function>(GetProcAddress(module, name)) : nullptr;
}
}

extern "C" DWORD WINAPI ProxyVerInstallFileA(DWORD flags, LPSTR src, LPSTR dest, LPSTR srcDir,
                                              LPSTR destDir, LPSTR currentDir, LPSTR temp, PUINT length) {
    auto function = systemExport<decltype(&VerInstallFileA)>("VerInstallFileA");
    return function ? function(flags, src, dest, srcDir, destDir, currentDir, temp, length) : VIF_CANNOTREADSRC;
}

extern "C" DWORD WINAPI ProxyVerInstallFileW(DWORD flags, LPWSTR src, LPWSTR dest, LPWSTR srcDir,
                                              LPWSTR destDir, LPWSTR currentDir, LPWSTR temp, PUINT length) {
    auto function = systemExport<decltype(&VerInstallFileW)>("VerInstallFileW");
    return function ? function(flags, src, dest, srcDir, destDir, currentDir, temp, length) : VIF_CANNOTREADSRC;
}
