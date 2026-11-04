#pragma once
#include <windows.h>

// Prepare private cache files and queue the Win32 file-open hooks. Call before
// enabling MinHook. The original game files are only read.
bool prepareFontOverlay(HMODULE module);
