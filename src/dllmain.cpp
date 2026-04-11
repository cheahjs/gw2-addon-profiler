#include <windows.h>
#include "addon.h"

static AddonDefinition_t s_addonDef = {};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Addon::g_hModule = hModule;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    s_addonDef.Signature   = 0xFFFA0010;
    s_addonDef.APIVersion  = NEXUS_API_VERSION;
    s_addonDef.Name        = "Nexus Profiler";
    s_addonDef.Version     = {0, 1, 0, 0};
    s_addonDef.Author      = "jscheah";
    s_addonDef.Description = "Tracy-based per-frame performance profiler for Nexus addons";
    s_addonDef.Load        = Addon::Load;
    s_addonDef.Unload      = Addon::Unload;
    s_addonDef.Flags       = AF_None;
    s_addonDef.Provider    = UP_None;
    s_addonDef.UpdateLink  = nullptr;
    return &s_addonDef;
}
