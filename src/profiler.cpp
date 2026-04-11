#include "profiler.h"

#include <tracy/Tracy.hpp>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

// ── Constants ────────────────────────────────────────────────

static constexpr size_t MAX_SLOTS = 128;
static constexpr int    FRAME_HISTORY_SIZE = 300;

// ── Per-slot data ────────────────────────────────────────────

struct RenderSlot {
    GUI_RENDER  original    = nullptr;
    ERenderType render_type = RT_Render;

    char   addon_name[256]   = {};
    char   callback_type[32] = {};
    char   zone_name[320]    = {};
    size_t zone_name_len     = 0;

    double   last_us    = 0;
    double   avg_us     = 0;
    double   peak_us    = 0;
    uint64_t call_count = 0;
    bool     active     = false;
};

// ── Module state ─────────────────────────────────────────────

static RenderSlot   g_slots[MAX_SLOTS];
static std::mutex   g_mutex;

static GUI_ADDRENDER g_origRegister   = nullptr;
static GUI_REMRENDER g_origDeregister = nullptr;

static AddonAPI_t*  g_api       = nullptr;
static HMODULE      g_ownModule = nullptr;
static LARGE_INTEGER g_perfFreq;
static std::atomic<bool> g_active{false};

// Frame timing
static LARGE_INTEGER g_lastFrameStart = {};
static double   g_frameTimeUs     = 0;
static double   g_frameTimeAvgUs  = 0;
static double   g_frameTimePeakUs = 0;
static uint64_t g_frameCount      = 0;

static float g_frameHistory[FRAME_HISTORY_SIZE] = {};
static int   g_frameHistoryOffset = 0;

// ── Helpers ──────────────────────────────────────────────────

static const char* RenderTypeName(ERenderType type) {
    switch (type) {
        case RT_PreRender:     return "PreRender";
        case RT_Render:        return "Render";
        case RT_PostRender:    return "PostRender";
        case RT_OptionsRender: return "Options";
        default:               return "Unknown";
    }
}

static void ResolveAddonName(GUI_RENDER callback, char* out, size_t outSize) {
    HMODULE hMod = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(callback), &hMod) && hMod) {

        // Try the Nexus GetAddonDef export first
        using GetAddonDefFn = AddonDefinition_t* (*)();
        auto fn = reinterpret_cast<GetAddonDefFn>(
            GetProcAddress(hMod, "GetAddonDef"));
        if (fn) {
            auto* def = fn();
            if (def && def->Name) {
                strncpy(out, def->Name, outSize - 1);
                out[outSize - 1] = '\0';
                return;
            }
        }

        // Fall back to the DLL filename
        char path[MAX_PATH];
        if (GetModuleFileNameA(hMod, path, MAX_PATH)) {
            const char* slash = strrchr(path, '\\');
            const char* name  = slash ? slash + 1 : path;
            strncpy(out, name, outSize - 1);
            out[outSize - 1] = '\0';
            return;
        }
    }
    strncpy(out, "Unknown", outSize - 1);
}

static size_t AllocateSlot(ERenderType type, GUI_RENDER callback) {
    for (size_t i = 0; i < MAX_SLOTS; i++) {
        if (!g_slots[i].active) {
            auto& s = g_slots[i];
            s.original    = callback;
            s.render_type = type;

            ResolveAddonName(callback, s.addon_name, sizeof(s.addon_name));
            strncpy(s.callback_type, RenderTypeName(type),
                    sizeof(s.callback_type) - 1);

            snprintf(s.zone_name, sizeof(s.zone_name),
                     "%s [%s]", s.addon_name, s.callback_type);
            s.zone_name_len = strlen(s.zone_name);

            s.last_us    = 0;
            s.avg_us     = 0;
            s.peak_us    = 0;
            s.call_count = 0;
            s.active     = true;
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

// ── Trampolines ─────────────────────────────────────────────
//
// Each template instantiation is a unique function whose address
// can be registered with Nexus as a render callback.  When called
// it wraps the original addon callback with a Tracy zone and QPC
// timing so both Tracy and the in-game overlay show per-addon cost.

template<size_t Index>
static void RenderTrampoline() {
    auto& slot = g_slots[Index];
    if (!slot.original) return;

    if (g_active.load(std::memory_order_relaxed)) {
        ZoneScoped;
        ZoneName(slot.zone_name, slot.zone_name_len);

        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);

        slot.original();

        QueryPerformanceCounter(&end);
        double elapsed =
            static_cast<double>(end.QuadPart - start.QuadPart)
            * 1e6 / g_perfFreq.QuadPart;

        slot.last_us = elapsed;
        // Exponential moving average (τ ≈ 20 samples)
        slot.avg_us  = slot.avg_us * 0.95 + elapsed * 0.05;
        if (elapsed > slot.peak_us) slot.peak_us = elapsed;
        slot.call_count++;
    } else {
        // Pass-through during shutdown
        slot.original();
    }
}

template<size_t... Is>
static auto MakeTrampolineArray(std::index_sequence<Is...>)
    -> std::array<GUI_RENDER, sizeof...(Is)>
{
    return {{ &RenderTrampoline<Is>... }};
}

static const auto g_trampolines =
    MakeTrampolineArray(std::make_index_sequence<MAX_SLOTS>{});

// ── Hooked Nexus functions ───────────────────────────────────

static void HookedGUIRegister(ERenderType type, GUI_RENDER callback) {
    if (!callback) return;

    // Detect whether the callback belongs to our own module
    HMODULE callerMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(callback), &callerMod);

    if (callerMod == g_ownModule) {
        // Our own callback -- register directly, no wrapping
        g_origRegister(type, callback);
        return;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    size_t idx = AllocateSlot(type, callback);
    if (idx != static_cast<size_t>(-1)) {
        g_origRegister(type, g_trampolines[idx]);

        char msg[512];
        snprintf(msg, sizeof(msg), "Profiling: %s [%s]",
                 g_slots[idx].addon_name, g_slots[idx].callback_type);
        TracyMessage(msg, strlen(msg));
        if (g_api) g_api->Log(LOGL_DEBUG, "NexusProfiler", msg);
    } else {
        // No free slots -- register the original directly
        g_origRegister(type, callback);
        if (g_api)
            g_api->Log(LOGL_WARNING, "NexusProfiler",
                       "No free profiling slots; callback registered without instrumentation");
    }
}

static void HookedGUIDeregister(GUI_RENDER callback) {
    if (!callback) return;

    // Our own callback -- pass through
    HMODULE callerMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(callback), &callerMod);

    if (callerMod == g_ownModule) {
        g_origDeregister(callback);
        return;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < MAX_SLOTS; i++) {
        if (g_slots[i].active && g_slots[i].original == callback) {
            g_origDeregister(g_trampolines[i]);

            char msg[512];
            snprintf(msg, sizeof(msg), "Stopped profiling: %s [%s]",
                     g_slots[i].addon_name, g_slots[i].callback_type);
            TracyMessage(msg, strlen(msg));

            g_slots[i].active   = false;
            g_slots[i].original = nullptr;
            return;
        }
    }

    // Callback was registered before our hook -- pass through
    g_origDeregister(callback);
}

// ── Public interface ─────────────────────────────────────────

namespace Profiler {

void Init(AddonAPI_t* api, HMODULE ownModule) {
    g_api       = api;
    g_ownModule = ownModule;
    QueryPerformanceFrequency(&g_perfFreq);

    // Hook GUI_Register so we can intercept addon callback registrations
    EMHStatus st = api->MinHook_Create(
        reinterpret_cast<void*>(api->GUI_Register),
        reinterpret_cast<void*>(&HookedGUIRegister),
        reinterpret_cast<void**>(&g_origRegister));

    if (st == MH_OK) {
        api->MinHook_Enable(reinterpret_cast<void*>(api->GUI_Register));
        api->Log(LOGL_INFO, "NexusProfiler", "Hooked GUI_Register");
    } else {
        char err[128];
        snprintf(err, sizeof(err),
                 "Failed to hook GUI_Register (MH status %d)", st);
        api->Log(LOGL_WARNING, "NexusProfiler", err);
        // Fallback: save the function pointer directly so frame timing
        // still works even without per-addon instrumentation.
        g_origRegister = api->GUI_Register;
    }

    st = api->MinHook_Create(
        reinterpret_cast<void*>(api->GUI_Deregister),
        reinterpret_cast<void*>(&HookedGUIDeregister),
        reinterpret_cast<void**>(&g_origDeregister));

    if (st == MH_OK) {
        api->MinHook_Enable(reinterpret_cast<void*>(api->GUI_Deregister));
        api->Log(LOGL_INFO, "NexusProfiler", "Hooked GUI_Deregister");
    } else {
        char err[128];
        snprintf(err, sizeof(err),
                 "Failed to hook GUI_Deregister (MH status %d)", st);
        api->Log(LOGL_WARNING, "NexusProfiler", err);
        g_origDeregister = api->GUI_Deregister;
    }

    g_active = true;
    api->Log(LOGL_INFO, "NexusProfiler", "Profiler initialised");
}

void Shutdown() {
    g_active = false;

    // Restore every wrapped callback to its original
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (size_t i = 0; i < MAX_SLOTS; i++) {
            if (g_slots[i].active && g_slots[i].original) {
                if (g_origDeregister)
                    g_origDeregister(g_trampolines[i]);
                if (g_origRegister)
                    g_origRegister(g_slots[i].render_type,
                                   g_slots[i].original);
                g_slots[i].active   = false;
                g_slots[i].original = nullptr;
            }
        }
    }

    // Remove hooks
    if (g_api) {
        g_api->MinHook_Disable(
            reinterpret_cast<void*>(g_api->GUI_Register));
        g_api->MinHook_Remove(
            reinterpret_cast<void*>(g_api->GUI_Register));
        g_api->MinHook_Disable(
            reinterpret_cast<void*>(g_api->GUI_Deregister));
        g_api->MinHook_Remove(
            reinterpret_cast<void*>(g_api->GUI_Deregister));
    }

    g_origRegister   = nullptr;
    g_origDeregister = nullptr;
}

// ── Frame timing ─────────────────────────────────────────────

void OnPreRender() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (g_lastFrameStart.QuadPart != 0) {
        g_frameTimeUs =
            static_cast<double>(now.QuadPart - g_lastFrameStart.QuadPart)
            * 1e6 / g_perfFreq.QuadPart;

        // Smooth average (τ ≈ 100 frames)
        if (g_frameCount < 100)
            g_frameTimeAvgUs = g_frameTimeUs;
        else
            g_frameTimeAvgUs = g_frameTimeAvgUs * 0.99 + g_frameTimeUs * 0.01;

        if (g_frameTimeUs > g_frameTimePeakUs)
            g_frameTimePeakUs = g_frameTimeUs;

        g_frameHistory[g_frameHistoryOffset] =
            static_cast<float>(g_frameTimeUs / 1000.0);
        g_frameHistoryOffset =
            (g_frameHistoryOffset + 1) % FRAME_HISTORY_SIZE;
    }
    g_lastFrameStart = now;
    g_frameCount++;

    FrameMark;
    TracyPlot("Frame Time (ms)", g_frameTimeUs / 1000.0);
}

void OnPostRender() {
    // Reserved for end-of-frame measurements in the future
}

// ── Event handlers ───────────────────────────────────────────

void OnAddonLoaded(void* eventArgs) {
    if (!eventArgs || !g_api) return;
    int32_t sig = *static_cast<int32_t*>(eventArgs);
    char msg[128];
    snprintf(msg, sizeof(msg), "Addon loaded (sig 0x%08X)", sig);
    TracyMessage(msg, strlen(msg));
    g_api->Log(LOGL_DEBUG, "NexusProfiler", msg);
}

void OnAddonUnloaded(void* eventArgs) {
    if (!eventArgs || !g_api) return;
    int32_t sig = *static_cast<int32_t*>(eventArgs);
    char msg[128];
    snprintf(msg, sizeof(msg), "Addon unloaded (sig 0x%08X)", sig);
    TracyMessage(msg, strlen(msg));
    g_api->Log(LOGL_DEBUG, "NexusProfiler", msg);
}

// ── Accessors ────────────────────────────────────────────────

double   GetFrameTimeMs()     { return g_frameTimeUs / 1000.0; }
double   GetFrameTimeAvgMs()  { return g_frameTimeAvgUs / 1000.0; }
double   GetFrameTimePeakMs() { return g_frameTimePeakUs / 1000.0; }
double   GetFPS()             { return g_frameTimeUs > 0 ? 1e6 / g_frameTimeUs : 0; }
uint64_t GetFrameCount()      { return g_frameCount; }

const float* GetFrameHistory()       { return g_frameHistory; }
int          GetFrameHistorySize()   { return FRAME_HISTORY_SIZE; }
int          GetFrameHistoryOffset() { return g_frameHistoryOffset; }

size_t GetMaxSlots() { return MAX_SLOTS; }

const CallbackStats* GetCallbackStats(size_t index) {
    static CallbackStats s_stats[MAX_SLOTS];
    if (index >= MAX_SLOTS) return nullptr;

    auto& slot  = g_slots[index];
    auto& stats = s_stats[index];
    stats.active = slot.active;
    if (slot.active) {
        memcpy(stats.addon_name,    slot.addon_name,    sizeof(stats.addon_name));
        memcpy(stats.callback_type, slot.callback_type, sizeof(stats.callback_type));
        stats.last_us    = slot.last_us;
        stats.avg_us     = slot.avg_us;
        stats.peak_us    = slot.peak_us;
        stats.call_count = slot.call_count;
    }
    return &stats;
}

size_t GetActiveCallbackCount() {
    size_t n = 0;
    for (size_t i = 0; i < MAX_SLOTS; i++)
        if (g_slots[i].active) n++;
    return n;
}

void ResetStats() {
    g_frameTimePeakUs = 0;
    g_frameTimeAvgUs  = g_frameTimeUs;

    for (auto& s : g_slots) {
        s.last_us    = 0;
        s.avg_us     = 0;
        s.peak_us    = 0;
        s.call_count = 0;
    }

    memset(g_frameHistory, 0, sizeof(g_frameHistory));
    g_frameHistoryOffset = 0;
}

} // namespace Profiler
