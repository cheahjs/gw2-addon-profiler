#include "ui.h"
#include "profiler.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>

static bool g_visible = true;

namespace UI {

bool  IsVisible()     { return g_visible; }
void  Toggle()        { g_visible = !g_visible; }
bool* GetVisiblePtr() { return &g_visible; }

void Render() {
    if (!g_visible) return;

    ImGui::SetNextWindowSize(ImVec2(540, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Nexus Profiler", &g_visible)) {
        ImGui::End();
        return;
    }

    // ── Frame timing header ──────────────────────────────────
    double frameMs = Profiler::GetFrameTimeMs();
    double avgMs   = Profiler::GetFrameTimeAvgMs();
    double peakMs  = Profiler::GetFrameTimePeakMs();
    double fps     = Profiler::GetFPS();

    ImGui::Text("Frame: %.2f ms (%.0f FPS)  |  Avg: %.2f ms  |  Peak: %.2f ms",
                frameMs, fps, avgMs, peakMs);

    ImGui::TextDisabled("Connect Tracy profiler to localhost:8086 for timeline view");

    ImGui::Separator();

    // ── Frame time graph ─────────────────────────────────────
    ImGui::PlotLines("##FrameTime",
                     Profiler::GetFrameHistory(),
                     Profiler::GetFrameHistorySize(),
                     Profiler::GetFrameHistoryOffset(),
                     "Frame Time (ms)",
                     0.0f, 33.3f,
                     ImVec2(-1, 60));

    ImGui::Separator();

    // ── Per-addon callback table ─────────────────────────────
    size_t activeCount = Profiler::GetActiveCallbackCount();
    ImGui::Text("Profiling %zu addon callback(s)", activeCount);

    if (activeCount > 0 &&
        ImGui::BeginTable("##AddonTimings", 5,
            ImGuiTableFlags_Borders   |
            ImGuiTableFlags_RowBg     |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY,
            ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn("Addon");
        ImGui::TableSetupColumn("Type",
            ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Last (us)",
            ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Avg (us)",
            ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Peak (us)",
            ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < Profiler::GetMaxSlots(); i++) {
            const auto* st = Profiler::GetCallbackStats(i);
            if (!st || !st->active) continue;

            ImGui::TableNextRow();

            // Addon name
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(st->addon_name);

            // Callback type
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(st->callback_type);

            // Last (color-coded)
            ImGui::TableNextColumn();
            ImVec4 color;
            if      (st->last_us < 100.0) color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
            else if (st->last_us < 500.0) color = ImVec4(0.9f, 0.8f, 0.1f, 1.0f);
            else                           color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
            ImGui::TextColored(color, "%.1f", st->last_us);

            // Avg
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", st->avg_us);

            // Peak
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", st->peak_us);
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset Stats")) {
        Profiler::ResetStats();
    }

    ImGui::End();
}

void RenderOptions() {
    ImGui::TextUnformatted("Nexus Profiler");
    ImGui::Separator();
    ImGui::Checkbox("Show Profiler Window", &g_visible);
    ImGui::TextDisabled(
        "Tip: Assign a keybind in Nexus settings under \"KB_NEXUS_PROFILER_TOGGLE\".");
}

} // namespace UI
