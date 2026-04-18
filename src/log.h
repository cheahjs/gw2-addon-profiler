#pragma once

#include <windows.h>

namespace Log {
    void Init();
    void Shutdown();
    void Write(const char* fmt, ...);
}
