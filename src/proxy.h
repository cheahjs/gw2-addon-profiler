#pragma once

#include <windows.h>

namespace D3D11Proxy {
    bool Init();
    void Shutdown();
    HMODULE GetRealD3D11();
}
