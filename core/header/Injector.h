#pragma once
#include <Windows.h>
#include <string>

namespace Injector {
    // Injection methods
    bool StandardInject(const char* dllPath, DWORD pid);
    bool ManualMapInject(const char* dllPath, DWORD pid, bool encrypt);

    // Process utilities
    DWORD FindProcess(const char* processName);

    // Logging callback
    using LogCallback = void(*)(const char* message, float r, float g, float b);
    void SetLogCallback(LogCallback callback);
}