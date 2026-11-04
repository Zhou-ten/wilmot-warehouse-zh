#pragma once

#include <windows.h>

// Queue graphics hooks before MinHook is enabled. An absent or mismatched
// help recipe leaves the regular text translations available.
void prepareHelpOverlay(HMODULE module, bool chinese, void (*logger)(const char*));
void setHelpLanguage(bool chinese);
