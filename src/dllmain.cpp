#include <windows.h>
#include <MinHook.h>
#include "proxy.h"
#include "profiler.h"

static HMODULE g_hModule = nullptr;

// ── GetProcAddress hook ──────────────────────────────────────
//
// By hooking GetProcAddress we intercept the moment any framework
// (Nexus, arcdps, etc.) looks up an addon's entry-point export.
// This lets us instrument the addon before the framework calls it.

static decltype(&GetProcAddress) g_origGetProcAddress = nullptr;

static FARPROC WINAPI HookedGetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    FARPROC result = g_origGetProcAddress(hModule, lpProcName);
    if (!result || !lpProcName || IS_INTRESOURCE(lpProcName))
        return result;

    // Never instrument our own module
    if (hModule == g_hModule) return result;

    if (strcmp(lpProcName, "get_init_addr") == 0) {
        FARPROC wrapped = Profiler::OnArcdpsAddonDetected(hModule, result);
        if (wrapped) return wrapped;
    }

    if (strcmp(lpProcName, "GetAddonDef") == 0) {
        FARPROC wrapped = Profiler::OnNexusAddonDetected(hModule, result);
        if (wrapped) return wrapped;
    }

    return result;
}

// ── DllMain ──────────────────────────────────────────────────

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;

        if (!D3D11Proxy::Init()) return FALSE;

        MH_Initialize();

        MH_CreateHook(reinterpret_cast<void*>(&GetProcAddress),
                       reinterpret_cast<void*>(&HookedGetProcAddress),
                       reinterpret_cast<void**>(&g_origGetProcAddress));
        MH_EnableHook(reinterpret_cast<void*>(&GetProcAddress));

        Profiler::Init();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        Profiler::Shutdown();

        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        D3D11Proxy::Shutdown();
    }
    return TRUE;
}
