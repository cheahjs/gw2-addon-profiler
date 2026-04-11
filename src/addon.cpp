#include "addon.h"
#include "profiler.h"
#include "ui.h"
#include <imgui.h>

HMODULE Addon::g_hModule = nullptr;
AddonAPI_t* Addon::API = nullptr;

static void OnPreRender() { Profiler::OnPreRender(); }
static void OnRender() { UI::Render(); }
static void OnPostRender() { Profiler::OnPostRender(); }
static void OnOptionsRender() { UI::RenderOptions(); }

static void OnToggleKeybind(const char*, bool isRelease) {
    if (!isRelease) {
        UI::Toggle();
    }
}

void Addon::Load(AddonAPI_t* api) {
    API = api;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<void*(*)(size_t, void*)>(api->ImguiMalloc),
        reinterpret_cast<void(*)(void*, void*)>(api->ImguiFree));

    // Install hooks BEFORE registering our own callbacks so we can
    // intercept other addons that load after us.
    Profiler::Init(api, g_hModule);

    // Register our render callbacks. These go through the hooked
    // GUI_Register but are detected as belonging to our module and
    // passed through without wrapping.
    api->GUI_Register(RT_PreRender, OnPreRender);
    api->GUI_Register(RT_Render, OnRender);
    api->GUI_Register(RT_PostRender, OnPostRender);
    api->GUI_Register(RT_OptionsRender, OnOptionsRender);

    // Keybind & close-on-escape
    api->InputBinds_RegisterWithString(
        "KB_NEXUS_PROFILER_TOGGLE", OnToggleKeybind, "(null)");
    api->GUI_RegisterCloseOnEscape("Nexus Profiler", UI::GetVisiblePtr());

    // Subscribe to addon lifecycle events for logging
    api->Events_Subscribe(EV_ADDON_LOADED, Profiler::OnAddonLoaded);
    api->Events_Subscribe(EV_ADDON_UNLOADED, Profiler::OnAddonUnloaded);

    api->Log(LOGL_INFO, "NexusProfiler",
             "Nexus Profiler loaded. Connect Tracy GUI to localhost:8086 for detailed profiling.");
}

void Addon::Unload() {
    AddonAPI_t* api = API;

    api->Events_Unsubscribe(EV_ADDON_LOADED, Profiler::OnAddonLoaded);
    api->Events_Unsubscribe(EV_ADDON_UNLOADED, Profiler::OnAddonUnloaded);

    api->InputBinds_Deregister("KB_NEXUS_PROFILER_TOGGLE");
    api->GUI_DeregisterCloseOnEscape("Nexus Profiler");

    api->GUI_Deregister(OnOptionsRender);
    api->GUI_Deregister(OnPostRender);
    api->GUI_Deregister(OnRender);
    api->GUI_Deregister(OnPreRender);

    Profiler::Shutdown();

    api->Log(LOGL_INFO, "NexusProfiler", "Nexus Profiler unloaded.");
}
