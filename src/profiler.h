#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace Profiler {

// ── Callback stat block (used by the UI) ─────────────────────
struct Stats {
    double   last_us;
    double   avg_us;
    double   peak_us;
    uint64_t count;
};

// ── Per-addon information visible to the UI ──────────────────
struct AddonInfo {
    char   name[256];
    bool   active;
    bool   is_arcdps;      // true = arcdps, false = nexus

    // arcdps per-callback stats
    Stats  imgui;
    Stats  combat;
    Stats  wnd;
    Stats  options;
};

// ── Per-render-callback info (Nexus GUI_RENDER) ──────────────
struct RenderInfo {
    char   addon_name[256];
    char   callback_type[32];
    bool   active;
    Stats  stats;
};

// ── Lifecycle ────────────────────────────────────────────────
void Init();
void Shutdown();
void OnFrameTick();

// ── Called by the GetProcAddress hook in dllmain ─────────────
FARPROC OnArcdpsAddonDetected(HMODULE module, FARPROC realAddr);
FARPROC OnNexusAddonDetected(HMODULE module, FARPROC realAddr);

// ── Accessors for the UI ─────────────────────────────────────
double   GetFrameTimeMs();
double   GetFrameTimeAvgMs();
double   GetFrameTimePeakMs();
double   GetFPS();

const float* GetFrameHistory();
int   GetFrameHistorySize();
int   GetFrameHistoryOffset();

size_t GetMaxAddons();
const AddonInfo* GetAddonInfo(size_t index);

size_t GetMaxRenderSlots();
const RenderInfo* GetRenderInfo(size_t index);

void ResetStats();

} // namespace Profiler
