#pragma once

#include <Nexus.h>
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace Profiler {

struct CallbackStats {
    char addon_name[256];
    char callback_type[32];
    double last_us;
    double avg_us;
    double peak_us;
    uint64_t call_count;
    bool active;
};

void Init(AddonAPI_t* api, HMODULE ownModule);
void Shutdown();

void OnPreRender();
void OnPostRender();

// Nexus event handlers
void OnAddonLoaded(void* eventArgs);
void OnAddonUnloaded(void* eventArgs);

// Frame timing
double GetFrameTimeMs();
double GetFrameTimeAvgMs();
double GetFrameTimePeakMs();
double GetFPS();
uint64_t GetFrameCount();

// Frame history for graph
const float* GetFrameHistory();
int GetFrameHistorySize();
int GetFrameHistoryOffset();

// Per-callback stats
size_t GetMaxSlots();
const CallbackStats* GetCallbackStats(size_t index);
size_t GetActiveCallbackCount();
void ResetStats();

} // namespace Profiler
