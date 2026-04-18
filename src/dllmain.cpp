#include <windows.h>
#include <MinHook.h>
#include "proxy.h"
#include "profiler.h"
#include "log.h"

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
        Log::Write("GetProcAddress: get_init_addr from module %p", hModule);
        FARPROC wrapped = Profiler::OnArcdpsAddonDetected(hModule, result);
        if (wrapped) Log::Write("  -> arcdps addon instrumented");
        else         Log::Write("  -> skipped");
        if (wrapped) return wrapped;
    }

    if (strcmp(lpProcName, "GetAddonDef") == 0) {
        Log::Write("GetProcAddress: GetAddonDef from module %p", hModule);
        FARPROC wrapped = Profiler::OnNexusAddonDetected(hModule, result);
        if (wrapped) Log::Write("  -> Nexus addon instrumented");
        else         Log::Write("  -> skipped");
        if (wrapped) return wrapped;
    }

    return result;
}

// ── DllMain ──────────────────────────────────────────────────

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;

        Log::Init();
        Log::Write("=== gw2-addon-profiler starting ===");

        Log::Write("Initializing D3D11 proxy...");
        if (!D3D11Proxy::Init()) {
            Log::Write("D3D11 proxy init FAILED");
            Log::Shutdown();
            return FALSE;
        }
        Log::Write("D3D11 proxy init OK");

        MH_STATUS mhStatus = MH_Initialize();
        Log::Write("MinHook init: %d", mhStatus);

        mhStatus = MH_CreateHook(reinterpret_cast<void*>(&GetProcAddress),
                       reinterpret_cast<void*>(&HookedGetProcAddress),
                       reinterpret_cast<void**>(&g_origGetProcAddress));
        Log::Write("GetProcAddress hook create: %d", mhStatus);

        mhStatus = MH_EnableHook(reinterpret_cast<void*>(&GetProcAddress));
        Log::Write("GetProcAddress hook enable: %d", mhStatus);

        Profiler::Init();
        Log::Write("Profiler initialized");
    }
    else if (reason == DLL_PROCESS_DETACH) {
        Log::Write("=== gw2-addon-profiler shutting down ===");

        Profiler::Shutdown();

        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        D3D11Proxy::Shutdown();

        Log::Write("Shutdown complete");
        Log::Shutdown();
    }
    return TRUE;
}
