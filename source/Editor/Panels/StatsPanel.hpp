#pragma once

#include "Editor/UI/Panel.hpp"
#include "Editor/History/CommandHistory.hpp"
#include "Assets/AssetManager.hpp"
#include "Core/Types.hpp"
#include "Core/Profiler.hpp"
#include "Core/Memory/MemoryStats.hpp"

#include <imgui.h>
#include <array>
#include <algorithm>
#include <numeric>
#include <string>

namespace RL::Editor
{
    class StatsPanel : public Panel
    {
    public:
        StatsPanel() : Panel("Stats##STATS") {}

        void SetStats(u32 fps, f32 frameTimeMs, u64 entities, u64 rendered,
                      u32 drawCalls, u64 tris)
        {
            m_fps         = fps;
            m_frameTimeMs = frameTimeMs;
            m_entities    = entities;
            m_rendered    = rendered;
            m_drawCalls   = drawCalls;
            m_tris        = tris;

            m_fpsHistory[m_historyHead] = static_cast<f32>(fps);
            m_historyHead = (m_historyHead + 1) % kHistorySize;
        }

        void OnRender() override
        {

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.05f, 1.0f));
            ImGui::Text("Performance");
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::Text("FPS:          %u", m_fps);
            ImGui::Text("Frame Time:   %.2f ms", m_frameTimeMs);

            char overlay[32];
            std::snprintf(overlay, sizeof(overlay), "%u FPS", m_fps);

            const f32 maxFps = *std::max_element(m_fpsHistory.begin(), m_fpsHistory.end()) + 1.0f;
            ImGui::PlotLines("##fps_graph",
                m_fpsHistory.data(), kHistorySize, m_historyHead,
                overlay, 0.0f, maxFps,
                ImVec2(0, 60));

            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.05f, 1.0f));
            ImGui::Text("Scene");
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::Text("Entities:     %llu", m_entities);
            ImGui::Text("Rendered:     %llu", m_rendered);
            ImGui::Text("Draw Calls:   %u",   m_drawCalls);
            ImGui::Text("Triangles:    %llu",  m_tris);

            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.05f, 1.0f));
            ImGui::Text("Memory");
            ImGui::PopStyleColor();
            ImGui::Separator();

            const auto snap = RL::Memory::MemoryStats::GetSnapshot();
            const auto proc = RL::Memory::MemoryStats::GetProcessMemory();
            ImGui::Text("Process RAM:  %.2f MB",
                proc.workingSetBytes / (1024.0 * 1024.0));
            ImGui::Text("Committed:    %.2f MB",
                proc.privateUsageBytes / (1024.0 * 1024.0));
            ImGui::Text("Subsystems:   %.2f MB",
                snap.totalAllocated / (1024.0 * 1024.0));

            if (ImGui::TreeNodeEx("Categories", ImGuiTreeNodeFlags_DefaultOpen))
            {
                using Cat = RL::Memory::MemoryCategory;
                for (u32 i = 0; i < static_cast<u32>(Cat::COUNT); ++i)
                {
                    const auto& cs = snap.categories[i];
                    if (cs.allocated == 0) continue;
                    const auto name = RL::Memory::MemoryStats::GetCategoryName(
                        static_cast<Cat>(i));
                    if (cs.allocated >= 1024 * 1024)
                    {
                        ImGui::Text("  %-10s %.2f MB",
                            name.data(), cs.allocated / (1024.0 * 1024.0));
                    }
                    else
                    {
                        ImGui::Text("  %-10s %.1f KB",
                            name.data(), cs.allocated / 1024.0);
                    }
                }
                ImGui::TreePop();
            }

            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.05f, 1.0f));
            ImGui::Text("Editor");
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::Text("Undo stack:   %u", static_cast<u32>(
                RL::Editor::CommandHistory::GetUndoCount()));
            ImGui::Text("Redo stack:   %u", static_cast<u32>(
                RL::Editor::CommandHistory::GetRedoCount()));
            ImGui::Text("Assets:       %zu cached (%zu pending)",
                RL::AssetManager::GetCachedAssetCount(),
                RL::AssetManager::GetPendingAsyncCount());

            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Frame Profiler (ms)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto& profiler = RL::FrameProfiler::Get();
                constexpr const char* kScopeNames[] = {
                    "Physics", "Animation", "Particles", "Audio", "Render"
                };
                constexpr f32 kFrameBudgetMs = 16.67f;

                for (usize i = 0; i < RL::FrameProfiler::kScopeCount; ++i)
                {
                    const auto scope = static_cast<RL::ProfileScope>(i);
                    const f32 ms = profiler.GetLastMs(scope);
                    const f32 frac = std::clamp(ms / kFrameBudgetMs, 0.0f, 1.0f);
                    char barOverlay[32];
                    std::snprintf(barOverlay, sizeof(barOverlay), "%.2f ms", ms);

                    ImGui::Text("%-10s", kScopeNames[i]);
                    ImGui::SameLine(85.0f);
                    ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f), barOverlay);
                }

                ImGui::Spacing();

                const auto& renderHist = profiler.GetHistory(RL::ProfileScope::Render);
                const i32 renderHead = static_cast<i32>(profiler.GetHistoryHead(RL::ProfileScope::Render));
                const f32 maxRender = (std::max)(1.0f, *std::max_element(renderHist.begin(), renderHist.end()));
                char renderOverlay[32];
                std::snprintf(renderOverlay, sizeof(renderOverlay), "Render: %.2f ms",
                    profiler.GetLastMs(RL::ProfileScope::Render));
                ImGui::PlotLines("##prof_render",
                    renderHist.data(),
                    static_cast<i32>(RL::FrameProfiler::kHistorySize),
                    renderHead,
                    renderOverlay, 0.0f, maxRender,
                    ImVec2(0, 48));

                const auto& physicsHist = profiler.GetHistory(RL::ProfileScope::Physics);
                const i32 physicsHead = static_cast<i32>(profiler.GetHistoryHead(RL::ProfileScope::Physics));
                const f32 maxPhysics = (std::max)(1.0f, *std::max_element(physicsHist.begin(), physicsHist.end()));
                char physicsOverlay[32];
                std::snprintf(physicsOverlay, sizeof(physicsOverlay), "Physics: %.2f ms",
                    profiler.GetLastMs(RL::ProfileScope::Physics));
                ImGui::PlotLines("##prof_physics",
                    physicsHist.data(),
                    static_cast<i32>(RL::FrameProfiler::kHistorySize),
                    physicsHead,
                    physicsOverlay, 0.0f, maxPhysics,
                    ImVec2(0, 48));
            }
        }

    private:
        static constexpr i32 kHistorySize = 128;

        u32  m_fps         = 0;
        f32  m_frameTimeMs = 0.0f;
        u64  m_entities    = 0;
        u64  m_rendered    = 0;
        u32  m_drawCalls   = 0;
        u64  m_tris        = 0;

        std::array<f32, kHistorySize> m_fpsHistory{};
        i32 m_historyHead = 0;
    };

}
