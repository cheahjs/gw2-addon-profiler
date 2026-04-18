#include "ui.h"
#include "profiler.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>

static bool g_visible = true;

namespace UI {

bool  IsVisible() { return g_visible; }
void  Toggle()    { g_visible = !g_visible; }

static void StatsColumns(const Profiler::Stats& s) {
    ImVec4 col;
    if      (s.last_us < 100.0) col = {0.2f,0.8f,0.2f,1.f};
    else if (s.last_us < 500.0) col = {0.9f,0.8f,0.1f,1.f};
    else                         col = {0.9f,0.2f,0.2f,1.f};
    ImGui::TableNextColumn(); ImGui::TextColored(col, "%.1f", s.last_us);
    ImGui::TableNextColumn(); ImGui::Text("%.1f", s.avg_us);
    ImGui::TableNextColumn(); ImGui::Text("%.1f", s.peak_us);
}

void Render() {
    if (!g_visible) return;

    ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("GW2 Addon Profiler", &g_visible)) { ImGui::End(); return; }

    // ── Frame timing ─────────────────────────────────────────
    double ms   = Profiler::GetFrameTimeMs();
    double avg  = Profiler::GetFrameTimeAvgMs();
    double peak = Profiler::GetFrameTimePeakMs();
    double fps  = Profiler::GetFPS();
    ImGui::Text("Frame: %.2f ms (%.0f FPS)  |  Avg: %.2f ms  |  Peak: %.2f ms",
                ms, fps, avg, peak);
    ImGui::TextDisabled("Connect Tracy to localhost:8086 for timeline view");
    ImGui::Separator();

    {
        const float* hist = Profiler::GetFrameHistory();
        int histSize = Profiler::GetFrameHistorySize();
        int histOff  = Profiler::GetFrameHistoryOffset();
        float graphH = 80.0f;
        float scaleMax = 33.3f;

        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, graphH);
        ImVec2 pos  = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Background
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          IM_COL32(20, 20, 30, 255));

        // Bars
        float barW = size.x / (float)histSize;
        for (int i = 0; i < histSize; i++) {
            int idx = (histOff + i) % histSize;
            float v = hist[idx];
            if (v <= 0.0f) continue;
            float h = (v / scaleMax) * graphH;
            if (h > graphH) h = graphH;

            float x = pos.x + i * barW;
            ImU32 col;
            if      (v < 16.7f) col = IM_COL32(70, 200, 120, 255);  // green: <60fps budget
            else if (v < 33.3f) col = IM_COL32(230, 200, 50, 255);  // yellow
            else                col = IM_COL32(230, 60, 60, 255);    // red

            dl->AddRectFilled(ImVec2(x, pos.y + graphH - h),
                              ImVec2(x + barW, pos.y + graphH), col);
        }

        // 16.7ms reference line (60 FPS)
        float lineY = pos.y + graphH - (16.7f / scaleMax) * graphH;
        dl->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + size.x, lineY),
                    IM_COL32(255, 255, 255, 60));

        // Border
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                    IM_COL32(80, 80, 100, 255));

        // Overlay text
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 4, pos.y + 2));
        ImGui::Text("Frame Time (ms)");

        // Advance cursor past the graph
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + ImGui::GetStyle().ItemSpacing.y));
    }
    ImGui::Separator();

    // ── arcdps addons ────────────────────────────────────────
    bool hasArcdps = false;
    for (size_t i = 0; i < Profiler::GetMaxAddons(); i++) {
        auto* a = Profiler::GetAddonInfo(i);
        if (a && a->active && a->is_arcdps) { hasArcdps = true; break; }
    }

    if (hasArcdps && ImGui::CollapsingHeader("arcdps Addons", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("##arc", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                ImVec2(0, 0)))
        {
            ImGui::TableSetupColumn("Addon");
            ImGui::TableSetupColumn("Last (us)", ImGuiTableColumnFlags_WidthFixed, 68);
            ImGui::TableSetupColumn("Avg (us)",  ImGuiTableColumnFlags_WidthFixed, 68);
            ImGui::TableSetupColumn("Peak (us)", ImGuiTableColumnFlags_WidthFixed, 68);
            ImGui::TableSetupColumn("Type",      ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < Profiler::GetMaxAddons(); i++) {
                auto* a = Profiler::GetAddonInfo(i);
                if (!a || !a->active || !a->is_arcdps) continue;

                auto row = [&](const char* type, const Profiler::Stats& s) {
                    if (s.count == 0) return;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(a->name);
                    StatsColumns(s);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(type);
                };
                row("imgui",   a->imgui);
                row("combat",  a->combat);
                row("wndproc", a->wnd);
                row("options", a->options);
            }
            ImGui::EndTable();
        }
    }

    // ── Nexus addons ─────────────────────────────────────────
    bool hasNexus = false;
    for (size_t i = 0; i < Profiler::GetMaxRenderSlots(); i++) {
        auto* r = Profiler::GetRenderInfo(i);
        if (r && r->active) { hasNexus = true; break; }
    }

    if (hasNexus && ImGui::CollapsingHeader("Nexus Addons", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("##nex", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                ImVec2(0, 0)))
        {
            ImGui::TableSetupColumn("Callback");
            ImGui::TableSetupColumn("Last (us)", ImGuiTableColumnFlags_WidthFixed, 68);
            ImGui::TableSetupColumn("Avg (us)",  ImGuiTableColumnFlags_WidthFixed, 68);
            ImGui::TableSetupColumn("Peak (us)", ImGuiTableColumnFlags_WidthFixed, 68);
            ImGui::TableSetupColumn("Type",      ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < Profiler::GetMaxRenderSlots(); i++) {
                auto* r = Profiler::GetRenderInfo(i);
                if (!r || !r->active) continue;

                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(r->addon_name);
                StatsColumns(r->stats);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(r->callback_type);
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset Stats")) Profiler::ResetStats();

    ImGui::End();
}

} // namespace UI
