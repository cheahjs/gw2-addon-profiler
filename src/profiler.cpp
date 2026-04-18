#include "profiler.h"
#include "log.h"

#include <MinHook.h>
// MinHook's MH_STATUS enum names collide with Nexus.h's EMHStatus;
// suppress the Nexus copy and use MH_STATUS directly.
typedef MH_STATUS EMHStatus;
#define NEXUS_NO_MHSTATUS
#include <Nexus.h>
#include <arcdps_structs.h>
#include <tracy/Tracy.hpp>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

// ═════════════════════════════════════════════════════════════
//  Constants
// ═════════════════════════════════════════════════════════════

static constexpr size_t MAX_ADDONS       = 32;
static constexpr size_t MAX_RENDER_SLOTS = 128;
static constexpr int    FRAME_HIST_SIZE  = 300;

// ═════════════════════════════════════════════════════════════
//  Timing helper (RAII)
// ═════════════════════════════════════════════════════════════

static LARGE_INTEGER g_perfFreq;

struct TimingScope {
    LARGE_INTEGER start;
    Profiler::Stats* st;
    TimingScope(Profiler::Stats* s) : st(s) { QueryPerformanceCounter(&start); }
    ~TimingScope() {
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        double us = double(end.QuadPart - start.QuadPart) * 1e6 / g_perfFreq.QuadPart;
        st->last_us = us;
        st->avg_us  = st->avg_us * 0.95 + us * 0.05;
        if (us > st->peak_us) st->peak_us = us;
        st->count++;
    }
};

// ═════════════════════════════════════════════════════════════
//  Addon slot (shared between arcdps and Nexus addons)
// ═════════════════════════════════════════════════════════════

struct AddonSlot {
    char    name[256]    = {};
    HMODULE module       = nullptr;
    bool    active       = false;
    bool    is_arcdps    = false;

    // arcdps --------------------------------------------------
    get_init_addr_t orig_get_init_addr = nullptr;
    mod_init_t      orig_mod_init      = nullptr; // real mod_init returned by get_init_addr
    arcdps_exports* exports            = nullptr;
    void* orig_imgui          = nullptr;
    void* orig_combat         = nullptr;
    void* orig_combat_local   = nullptr;
    void* orig_wnd_nofilter   = nullptr;
    void* orig_wnd_filter     = nullptr;
    void* orig_options_end    = nullptr;
    void* orig_options_windows= nullptr;

    Profiler::Stats stats_imgui   = {};
    Profiler::Stats stats_combat  = {};
    Profiler::Stats stats_wnd     = {};
    Profiler::Stats stats_options = {};

    // nexus ---------------------------------------------------
    using GetAddonDefFn = AddonDefinition_t*(*)();
    GetAddonDefFn orig_getaddondef = nullptr; // MinHook trampoline
    ADDON_LOAD    orig_load        = nullptr;
    AddonDefinition_t* nexus_def   = nullptr;
};

static AddonSlot g_addons[MAX_ADDONS];
static std::mutex g_addonMutex;

// ═════════════════════════════════════════════════════════════
//  Nexus render-callback slots (filled by GUI_Register hook)
// ═════════════════════════════════════════════════════════════

struct RenderSlot {
    GUI_RENDER  original    = nullptr;
    ERenderType render_type = RT_Render;
    size_t      addon_idx   = SIZE_MAX;
    char zone_name[320]     = {};
    size_t zone_name_len    = 0;
    Profiler::Stats stats   = {};
    bool active             = false;
};

static RenderSlot g_renders[MAX_RENDER_SLOTS];

// ═════════════════════════════════════════════════════════════
//  Frame timing
// ═════════════════════════════════════════════════════════════

static LARGE_INTEGER g_lastFrameStart = {};
static double   g_ftUs     = 0, g_ftAvgUs = 0, g_ftPeakUs = 0;
static float    g_ftHist[FRAME_HIST_SIZE] = {};
static int      g_ftHistOff = 0;
static uint64_t g_frameCount = 0;

// ═════════════════════════════════════════════════════════════
//  Nexus GUI_Register / GUI_Deregister hook state
// ═════════════════════════════════════════════════════════════

static GUI_ADDRENDER g_origGUIRegister   = nullptr;
static GUI_REMRENDER g_origGUIDeregister = nullptr;
static bool          g_guiHooked         = false;
static HMODULE       g_ownModule         = nullptr; // set in Init

// ═════════════════════════════════════════════════════════════
//  Helpers
// ═════════════════════════════════════════════════════════════

static size_t AllocAddonSlot(HMODULE mod) {
    std::lock_guard<std::mutex> lk(g_addonMutex);
    // Already tracked?
    for (size_t i = 0; i < MAX_ADDONS; i++)
        if (g_addons[i].active && g_addons[i].module == mod) return i;
    // Allocate new
    for (size_t i = 0; i < MAX_ADDONS; i++) {
        if (!g_addons[i].active) {
            g_addons[i] = AddonSlot{};
            g_addons[i].module = mod;
            g_addons[i].active = true;
            return i;
        }
    }
    return SIZE_MAX;
}

static const char* RenderTypeName(ERenderType t) {
    switch (t) {
        case RT_PreRender:     return "PreRender";
        case RT_Render:        return "Render";
        case RT_PostRender:    return "PostRender";
        case RT_OptionsRender: return "Options";
        default:               return "?";
    }
}

static void ResolveName(HMODULE mod, char* out, size_t n) {
    char path[MAX_PATH];
    if (GetModuleFileNameA(mod, path, MAX_PATH)) {
        const char* s = strrchr(path, '\\');
        strncpy(out, s ? s + 1 : path, n - 1);
        out[n - 1] = '\0';
    } else {
        strncpy(out, "Unknown", n - 1);
    }
}

// ═════════════════════════════════════════════════════════════
//  arcdps callback trampolines
// ═════════════════════════════════════════════════════════════

// imgui: void(uint32_t)
template<size_t I> static void ArcImguiT(uint32_t nc) {
    auto& s = g_addons[I];
    ZoneScoped; ZoneName(s.name, strlen(s.name));
    TimingScope t(&s.stats_imgui);
    ((imgui_callback_t)s.orig_imgui)(nc);
}

// combat: void(cbtevent*,ag*,ag*,const char*,uint64_t,uint64_t)
template<size_t I> static void ArcCombatT(
    cbtevent* e, ag* src, ag* dst, const char* sk, uint64_t id, uint64_t rev)
{
    auto& s = g_addons[I];
    ZoneScoped;
    TimingScope t(&s.stats_combat);
    ((combat_callback_t)s.orig_combat)(e, src, dst, sk, id, rev);
}
template<size_t I> static void ArcCombatLocalT(
    cbtevent* e, ag* src, ag* dst, const char* sk, uint64_t id, uint64_t rev)
{
    auto& s = g_addons[I];
    ZoneScoped;
    TimingScope t(&s.stats_combat);
    ((combat_callback_t)s.orig_combat_local)(e, src, dst, sk, id, rev);
}

// wnd: UINT(HWND,UINT,WPARAM,LPARAM) — lightweight, no Tracy zone
template<size_t I> static UINT ArcWndNoFilterT(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto& s = g_addons[I];
    TimingScope t(&s.stats_wnd);
    return ((wndproc_callback_t)s.orig_wnd_nofilter)(h, m, w, l);
}
template<size_t I> static UINT ArcWndFilterT(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto& s = g_addons[I];
    return ((wndproc_callback_t)s.orig_wnd_filter)(h, m, w, l);
}

// options_end: void()
template<size_t I> static void ArcOptEndT() {
    auto& s = g_addons[I];
    TimingScope t(&s.stats_options);
    ((options_callback_t)s.orig_options_end)();
}
// options_windows: void(const char*)
template<size_t I> static void ArcOptWinT(const char* wn) {
    auto& s = g_addons[I];
    ((options_windows_callback_t)s.orig_options_windows)(wn);
}

// ── Build function-pointer arrays for each callback type ─────

template<size_t... Is> static auto MkArcImgui(std::index_sequence<Is...>)
    -> std::array<void(*)(uint32_t), sizeof...(Is)> { return {{ &ArcImguiT<Is>... }}; }
template<size_t... Is> static auto MkArcCombat(std::index_sequence<Is...>)
    -> std::array<combat_callback_t, sizeof...(Is)> { return {{ &ArcCombatT<Is>... }}; }
template<size_t... Is> static auto MkArcCombatLocal(std::index_sequence<Is...>)
    -> std::array<combat_callback_t, sizeof...(Is)> { return {{ &ArcCombatLocalT<Is>... }}; }
template<size_t... Is> static auto MkArcWndNF(std::index_sequence<Is...>)
    -> std::array<wndproc_callback_t, sizeof...(Is)> { return {{ &ArcWndNoFilterT<Is>... }}; }
template<size_t... Is> static auto MkArcWndF(std::index_sequence<Is...>)
    -> std::array<wndproc_callback_t, sizeof...(Is)> { return {{ &ArcWndFilterT<Is>... }}; }
template<size_t... Is> static auto MkArcOptEnd(std::index_sequence<Is...>)
    -> std::array<options_callback_t, sizeof...(Is)> { return {{ &ArcOptEndT<Is>... }}; }
template<size_t... Is> static auto MkArcOptWin(std::index_sequence<Is...>)
    -> std::array<options_windows_callback_t, sizeof...(Is)> { return {{ &ArcOptWinT<Is>... }}; }

static const auto kArcImgui      = MkArcImgui(std::make_index_sequence<MAX_ADDONS>{});
static const auto kArcCombat     = MkArcCombat(std::make_index_sequence<MAX_ADDONS>{});
static const auto kArcCombatLoc  = MkArcCombatLocal(std::make_index_sequence<MAX_ADDONS>{});
static const auto kArcWndNF      = MkArcWndNF(std::make_index_sequence<MAX_ADDONS>{});
static const auto kArcWndF       = MkArcWndF(std::make_index_sequence<MAX_ADDONS>{});
static const auto kArcOptEnd     = MkArcOptEnd(std::make_index_sequence<MAX_ADDONS>{});
static const auto kArcOptWin     = MkArcOptWin(std::make_index_sequence<MAX_ADDONS>{});

// ═════════════════════════════════════════════════════════════
//  arcdps mod_init trampoline (step 2: called by arcdps to get exports)
// ═════════════════════════════════════════════════════════════

template<size_t I>
static arcdps_exports* ModInitT() {
    auto& slot = g_addons[I];
    auto* ex = slot.orig_mod_init();
    if (!ex) return ex;

    slot.exports = ex;
    if (ex->out_name) strncpy(slot.name, ex->out_name, sizeof(slot.name) - 1);

    Log::Write("arcdps mod_init<%zu>: name='%s' sig=0x%x imgui=%p combat=%p",
               I, slot.name, ex->sig, ex->imgui, ex->combat);

    // Patch callbacks in-place (VirtualProtect for read-only PE sections)
    DWORD oldProt = 0;
    VirtualProtect(ex, sizeof(*ex), PAGE_READWRITE, &oldProt);

    if (ex->imgui)          { slot.orig_imgui         = ex->imgui;          ex->imgui          = (void*)kArcImgui[I]; }
    if (ex->combat)         { slot.orig_combat        = ex->combat;         ex->combat         = (void*)kArcCombat[I]; }
    if (ex->combat_local)   { slot.orig_combat_local  = ex->combat_local;   ex->combat_local   = (void*)kArcCombatLoc[I]; }
    if (ex->wnd_nofilter)   { slot.orig_wnd_nofilter  = ex->wnd_nofilter;   ex->wnd_nofilter   = (void*)kArcWndNF[I]; }
    if (ex->wnd_filter)     { slot.orig_wnd_filter    = ex->wnd_filter;     ex->wnd_filter     = (void*)kArcWndF[I]; }
    if (ex->options_end)    { slot.orig_options_end    = ex->options_end;    ex->options_end    = (void*)kArcOptEnd[I]; }
    if (ex->options_windows){ slot.orig_options_windows= ex->options_windows;ex->options_windows= (void*)kArcOptWin[I]; }

    VirtualProtect(ex, sizeof(*ex), oldProt, &oldProt);

    char msg[512];
    snprintf(msg, sizeof(msg), "Instrumented arcdps addon: %s", slot.name);
    TracyMessage(msg, strlen(msg));

    return ex;
}

template<size_t... Is> static auto MkModInit(std::index_sequence<Is...>)
    -> std::array<mod_init_t, sizeof...(Is)> { return {{ &ModInitT<Is>... }}; }
static const auto kModInit = MkModInit(std::make_index_sequence<MAX_ADDONS>{});

// ═════════════════════════════════════════════════════════════
//  arcdps get_init_addr trampoline (step 1: returns mod_init fn ptr)
// ═════════════════════════════════════════════════════════════

template<size_t I>
static mod_init_t GetInitAddrT(
    const char* arcv, void* imctx, void* d3d, HMODULE arcdll,
    void* mfn, void* ffn, uint32_t d3dv)
{
    auto& slot = g_addons[I];
    mod_init_t real_init = slot.orig_get_init_addr(arcv, imctx, d3d, arcdll, mfn, ffn, d3dv);
    if (!real_init) return real_init;

    Log::Write("GetInitAddrT<%zu>: mod_init=%p, wrapping with %p", I, real_init, kModInit[I]);
    slot.orig_mod_init = real_init;
    return kModInit[I];
}

template<size_t... Is> static auto MkGetInitAddr(std::index_sequence<Is...>)
    -> std::array<get_init_addr_t, sizeof...(Is)> { return {{ &GetInitAddrT<Is>... }}; }
static const auto kGetInitAddr = MkGetInitAddr(std::make_index_sequence<MAX_ADDONS>{});

// ═════════════════════════════════════════════════════════════
//  Nexus render-callback trampolines (void())
// ═════════════════════════════════════════════════════════════

template<size_t I> static void RenderT() {
    auto& r = g_renders[I];
    if (!r.original) return;
    ZoneScoped; ZoneName(r.zone_name, r.zone_name_len);
    TimingScope t(&r.stats);
    r.original();
}

template<size_t... Is> static auto MkRender(std::index_sequence<Is...>)
    -> std::array<GUI_RENDER, sizeof...(Is)> { return {{ &RenderT<Is>... }}; }
static const auto kRender = MkRender(std::make_index_sequence<MAX_RENDER_SLOTS>{});

// ═════════════════════════════════════════════════════════════
//  Nexus GUI_Register / GUI_Deregister hooks
// ═════════════════════════════════════════════════════════════

static void HookedGUIRegister(ERenderType type, GUI_RENDER cb) {
    if (!cb) return;

    // Resolve addon name BEFORE acquiring the lock, because GetProcAddress
    // goes through our hook which may re-enter AllocAddonSlot and deadlock.
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(cb), &hMod);

    char addonName[256] = "Unknown";
    if (hMod) {
        // Check if we already have a name for this module from addon detection
        bool found = false;
        {
            std::lock_guard<std::mutex> lk(g_addonMutex);
            for (size_t i = 0; i < MAX_ADDONS; i++) {
                if (g_addons[i].active && g_addons[i].module == hMod && g_addons[i].name[0]) {
                    strncpy(addonName, g_addons[i].name, sizeof(addonName) - 1);
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            ResolveName(hMod, addonName, sizeof(addonName));
    }

    // Find a free render slot and wrap
    std::lock_guard<std::mutex> lk(g_addonMutex);
    for (size_t i = 0; i < MAX_RENDER_SLOTS; i++) {
        if (!g_renders[i].active) {
            auto& r = g_renders[i];
            r.original    = cb;
            r.render_type = type;
            r.active      = true;

            snprintf(r.zone_name, sizeof(r.zone_name), "%s [%s]",
                     addonName, RenderTypeName(type));
            r.zone_name_len = strlen(r.zone_name);

            g_origGUIRegister(type, kRender[i]);
            return;
        }
    }
    // No free slots — pass through
    g_origGUIRegister(type, cb);
}

static void HookedGUIDeregister(GUI_RENDER cb) {
    if (!cb) return;
    std::lock_guard<std::mutex> lk(g_addonMutex);
    for (size_t i = 0; i < MAX_RENDER_SLOTS; i++) {
        if (g_renders[i].active && g_renders[i].original == cb) {
            g_origGUIDeregister(kRender[i]);
            g_renders[i].active   = false;
            g_renders[i].original = nullptr;
            return;
        }
    }
    g_origGUIDeregister(cb);
}

static void InstallGUIHooks(AddonAPI_t* api) {
    if (g_guiHooked) return;
    Log::Write("Installing Nexus GUI hooks (Register=%p, Deregister=%p)",
               api->GUI_Register, api->GUI_Deregister);
    MH_STATUS st;
    st = MH_CreateHook(reinterpret_cast<void*>(api->GUI_Register),
                   reinterpret_cast<void*>(&HookedGUIRegister),
                   reinterpret_cast<void**>(&g_origGUIRegister));
    Log::Write("  GUI_Register hook create: %d", st);
    st = MH_EnableHook(reinterpret_cast<void*>(api->GUI_Register));
    Log::Write("  GUI_Register hook enable: %d", st);

    st = MH_CreateHook(reinterpret_cast<void*>(api->GUI_Deregister),
                   reinterpret_cast<void*>(&HookedGUIDeregister),
                   reinterpret_cast<void**>(&g_origGUIDeregister));
    Log::Write("  GUI_Deregister hook create: %d", st);
    st = MH_EnableHook(reinterpret_cast<void*>(api->GUI_Deregister));
    Log::Write("  GUI_Deregister hook enable: %d", st);
    g_guiHooked = true;
}

// ═════════════════════════════════════════════════════════════
//  Nexus Load wrapper trampoline
// ═════════════════════════════════════════════════════════════

template<size_t I> static void LoadT(AddonAPI_t* api) {
    auto& slot = g_addons[I];
    // Hook GUI_Register the first time any Nexus addon loads
    InstallGUIHooks(api);
    // Call the addon's real Load
    slot.orig_load(api);
}

template<size_t... Is> static auto MkLoad(std::index_sequence<Is...>)
    -> std::array<ADDON_LOAD, sizeof...(Is)> { return {{ &LoadT<Is>... }}; }
static const auto kLoad = MkLoad(std::make_index_sequence<MAX_ADDONS>{});

// ═════════════════════════════════════════════════════════════
//  Nexus GetAddonDef wrapper trampoline
// ═════════════════════════════════════════════════════════════

template<size_t I> static AddonDefinition_t* GetAddonDefT() {
    auto& slot = g_addons[I];
    auto* def = slot.orig_getaddondef();
    if (def && def->Load && def->Load != kLoad[I]) {
        slot.orig_load = def->Load;
        slot.nexus_def = def;
        // The AddonDefinition_t may live in the addon's read-only section,
        // so we must make the page writable before patching the Load pointer.
        DWORD oldProt = 0;
        VirtualProtect(&def->Load, sizeof(def->Load), PAGE_READWRITE, &oldProt);
        def->Load = kLoad[I];
        VirtualProtect(&def->Load, sizeof(def->Load), oldProt, &oldProt);
        if (def->Name)
            strncpy(slot.name, def->Name, sizeof(slot.name) - 1);
    }
    return def;
}

template<size_t... Is> static auto MkGetAddonDef(std::index_sequence<Is...>)
    -> std::array<AddonSlot::GetAddonDefFn, sizeof...(Is)> { return {{ &GetAddonDefT<Is>... }}; }
static const auto kGetAddonDef = MkGetAddonDef(std::make_index_sequence<MAX_ADDONS>{});

// ═════════════════════════════════════════════════════════════
//  Public: addon detection (called from GetProcAddress hook)
// ═════════════════════════════════════════════════════════════

namespace Profiler {

FARPROC OnArcdpsAddonDetected(HMODULE module, FARPROC realAddr) {
    size_t idx = AllocAddonSlot(module);
    if (idx == SIZE_MAX) {
        Log::Write("arcdps addon: no free slot for module %p", module);
        return nullptr;
    }

    auto& slot = g_addons[idx];
    // If already tracked as a Nexus addon, skip arcdps instrumentation to
    // avoid corrupting the slot state (dual addons like arcdps_integration64).
    if (!slot.is_arcdps && slot.orig_getaddondef) {
        Log::Write("arcdps addon: module %p ('%s') already tracked as Nexus addon, skipping",
                   module, slot.name);
        return nullptr;
    }
    slot.is_arcdps = true;
    ResolveName(module, slot.name, sizeof(slot.name));
    Log::Write("arcdps addon detected: %s (module=%p, slot=%zu)", slot.name, module, idx);

    // Store the original function pointer directly — no MinHook needed.
    // We return our trampoline from GetProcAddress instead.
    slot.orig_get_init_addr = reinterpret_cast<get_init_addr_t>(realAddr);
    Log::Write("  get_init_addr: orig=%p, trampoline=%p", realAddr, kGetInitAddr[idx]);

    return reinterpret_cast<FARPROC>(kGetInitAddr[idx]);
}

FARPROC OnNexusAddonDetected(HMODULE module, FARPROC realAddr) {
    size_t idx = AllocAddonSlot(module);
    if (idx == SIZE_MAX) {
        Log::Write("Nexus addon: no free slot for module %p", module);
        return nullptr;
    }

    auto& slot = g_addons[idx];
    // If already tracked as an arcdps addon, skip Nexus instrumentation.
    if (slot.is_arcdps && slot.orig_get_init_addr) {
        Log::Write("Nexus addon: module %p ('%s') already tracked as arcdps addon, skipping",
                   module, slot.name);
        return nullptr;
    }
    slot.is_arcdps = false;
    ResolveName(module, slot.name, sizeof(slot.name));
    Log::Write("Nexus addon detected: %s (module=%p, slot=%zu)", slot.name, module, idx);

    // Store the original function pointer directly — no MinHook needed.
    // We return our trampoline from GetProcAddress instead.
    slot.orig_getaddondef = reinterpret_cast<AddonSlot::GetAddonDefFn>(realAddr);
    Log::Write("  GetAddonDef: orig=%p, trampoline=%p", realAddr, kGetAddonDef[idx]);

    return reinterpret_cast<FARPROC>(kGetAddonDef[idx]);
}

// ═════════════════════════════════════════════════════════════
//  Lifecycle & frame timing
// ═════════════════════════════════════════════════════════════

void Init() {
    QueryPerformanceFrequency(&g_perfFreq);
    // g_ownModule is set by dllmain — we read it via GetModuleHandleExW
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Init), &g_ownModule);
}

void Shutdown() {
    // Restore Nexus render callbacks
    if (g_guiHooked) {
        for (size_t i = 0; i < MAX_RENDER_SLOTS; i++) {
            if (g_renders[i].active && g_renders[i].original) {
                if (g_origGUIDeregister) g_origGUIDeregister(kRender[i]);
                if (g_origGUIRegister)   g_origGUIRegister(g_renders[i].render_type,
                                                            g_renders[i].original);
                g_renders[i].active = false;
            }
        }
    }
    // arcdps: restore original callback pointers in the exports structs
    for (size_t i = 0; i < MAX_ADDONS; i++) {
        auto& s = g_addons[i];
        if (!s.active || !s.is_arcdps || !s.exports) continue;
        DWORD oldProt = 0;
        VirtualProtect(s.exports, sizeof(*s.exports), PAGE_READWRITE, &oldProt);
        if (s.orig_imgui)          s.exports->imgui          = s.orig_imgui;
        if (s.orig_combat)         s.exports->combat         = s.orig_combat;
        if (s.orig_combat_local)   s.exports->combat_local   = s.orig_combat_local;
        if (s.orig_wnd_nofilter)   s.exports->wnd_nofilter   = s.orig_wnd_nofilter;
        if (s.orig_wnd_filter)     s.exports->wnd_filter     = s.orig_wnd_filter;
        if (s.orig_options_end)    s.exports->options_end     = s.orig_options_end;
        if (s.orig_options_windows)s.exports->options_windows = s.orig_options_windows;
        VirtualProtect(s.exports, sizeof(*s.exports), oldProt, &oldProt);
    }
    // Hooks are removed globally by MH_DisableHook(MH_ALL_HOOKS) in dllmain
}

void OnFrameTick() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (g_lastFrameStart.QuadPart) {
        g_ftUs = double(now.QuadPart - g_lastFrameStart.QuadPart) * 1e6 / g_perfFreq.QuadPart;
        g_ftAvgUs = (g_frameCount < 100) ? g_ftUs : g_ftAvgUs * 0.99 + g_ftUs * 0.01;
        if (g_ftUs > g_ftPeakUs) g_ftPeakUs = g_ftUs;
        g_ftHist[g_ftHistOff] = float(g_ftUs / 1000.0);
        g_ftHistOff = (g_ftHistOff + 1) % FRAME_HIST_SIZE;
    }
    g_lastFrameStart = now;
    g_frameCount++;
    FrameMark;
    TracyPlot("Frame Time (ms)", g_ftUs / 1000.0);
}

// ═════════════════════════════════════════════════════════════
//  UI accessors
// ═════════════════════════════════════════════════════════════

double GetFrameTimeMs()     { return g_ftUs / 1000.0; }
double GetFrameTimeAvgMs()  { return g_ftAvgUs / 1000.0; }
double GetFrameTimePeakMs() { return g_ftPeakUs / 1000.0; }
double GetFPS()             { return g_ftUs > 0 ? 1e6 / g_ftUs : 0; }

const float* GetFrameHistory()     { return g_ftHist; }
int GetFrameHistorySize()          { return FRAME_HIST_SIZE; }
int GetFrameHistoryOffset()        { return g_ftHistOff; }

size_t GetMaxAddons() { return MAX_ADDONS; }

const AddonInfo* GetAddonInfo(size_t i) {
    static AddonInfo s_info[MAX_ADDONS];
    if (i >= MAX_ADDONS) return nullptr;
    auto& slot = g_addons[i];
    auto& info = s_info[i];
    info.active     = slot.active;
    info.is_arcdps  = slot.is_arcdps;
    if (slot.active) {
        memcpy(info.name, slot.name, sizeof(info.name));
        info.imgui   = slot.stats_imgui;
        info.combat  = slot.stats_combat;
        info.wnd     = slot.stats_wnd;
        info.options = slot.stats_options;
    }
    return &info;
}

size_t GetMaxRenderSlots() { return MAX_RENDER_SLOTS; }

const RenderInfo* GetRenderInfo(size_t i) {
    static RenderInfo s_ri[MAX_RENDER_SLOTS];
    if (i >= MAX_RENDER_SLOTS) return nullptr;
    auto& r  = g_renders[i];
    auto& ri = s_ri[i];
    ri.active = r.active;
    if (r.active) {
        // Pull addon name from the zone_name or from the addon slot
        HMODULE hm = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(r.original), &hm);
        // Use cached zone_name which already has "AddonName [Type]"
        memcpy(ri.addon_name, r.zone_name, sizeof(ri.addon_name));
        strncpy(ri.callback_type, RenderTypeName(r.render_type), sizeof(ri.callback_type) - 1);
        ri.stats = r.stats;
    }
    return &ri;
}

void ResetStats() {
    g_ftPeakUs = 0; g_ftAvgUs = g_ftUs;
    memset(g_ftHist, 0, sizeof(g_ftHist)); g_ftHistOff = 0;
    for (auto& s : g_addons) { s.stats_imgui = {}; s.stats_combat = {}; s.stats_wnd = {}; s.stats_options = {}; }
    for (auto& r : g_renders) { r.stats = {}; }
}

} // namespace Profiler
