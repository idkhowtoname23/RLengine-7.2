#pragma once

#include "Core/Types.hpp"
#include "Graphics/DX12/DX12Common.hpp"
#include "Graphics/RHI/RHIInterfaces.hpp"
#include "Editor/LuaConsole.hpp"
#include "Editor/SceneBuilder.hpp"
#include "Scene/StressTest.hpp"
#include "Editor/UI/PanelManager.hpp"
#include "Editor/Panels/SceneHierarchyPanel.hpp"
#include "Editor/Panels/InspectorPanel.hpp"
#include "Editor/Panels/ContentBrowserPanel.hpp"
#include "Editor/Panels/StatsPanel.hpp"

#include <expected>
#include <string>

struct HWND__;
typedef HWND__* HWND;
struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12DescriptorHeap;
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace RL
{
    class Timer;
    class Scene;
    class ScriptEngine;
    class RenderSystem;
    class TextureManager;

    enum class EditorBackend
    {
        DirectX12,
        DirectX11
    };

    enum class WorkspacePreset : u8
    {
        Default = 0,
        Modeling,
        Animation,
        Scripting,
        Debug
    };

    class EditorLayer
    {
    public:
        EditorLayer() = default;
        ~EditorLayer();

        EditorLayer(const EditorLayer&) = delete;
        EditorLayer& operator=(const EditorLayer&) = delete;

        EditorLayer(EditorLayer&&) noexcept = default;
        EditorLayer& operator=(EditorLayer&&) noexcept = default;

        [[nodiscard]] std::expected<void, std::string> Initialize(
            HWND hwnd,
            ID3D12Device* device,
            u32 numFrames,
            DXGI_FORMAT rtvFormat,
            ScriptEngine* scriptEngine = nullptr);

        [[nodiscard]] std::expected<void, std::string> InitializeDX11(
            HWND hwnd,
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            ScriptEngine* scriptEngine = nullptr);

        void Shutdown() noexcept;

        void BeginFrame();
        void RenderUI(
            const Timer& timer,
            const RHI::IRHIDevice& device,
            Scene& scene,
            u32 renderedEntities,
            RenderSystem* renderSystem = nullptr,
            TextureManager* textureManager = nullptr,
            ScriptEngine* scriptEngine = nullptr);
        void EndFrame(ID3D12GraphicsCommandList* cmdList);
        void EndFrameDX11();

        [[nodiscard]] LuaConsole& GetLuaConsole() noexcept { return m_luaConsole; }
        [[nodiscard]] const LuaConsole& GetLuaConsole() const noexcept { return m_luaConsole; }

        [[nodiscard]] SceneBuilder& GetSceneBuilder() noexcept { return m_sceneBuilder; }
        [[nodiscard]] const SceneBuilder& GetSceneBuilder() const noexcept { return m_sceneBuilder; }

        [[nodiscard]] StressTest& GetStressTest() noexcept { return m_stressTest; }
        [[nodiscard]] const StressTest& GetStressTest() const noexcept { return m_stressTest; }

        [[nodiscard]] Editor::PanelManager& GetPanelManager() noexcept { return m_panelManager; }
        void ToggleBlenderPanels() noexcept { m_showBlenderPanels = !m_showBlenderPanels; }
        [[nodiscard]] bool AreBlenderPanelsVisible() const noexcept { return m_showBlenderPanels; }

        void ApplyWorkspacePreset(WorkspacePreset preset);
        [[nodiscard]] WorkspacePreset GetActiveWorkspacePreset() const noexcept { return m_activePreset; }

        [[nodiscard]] bool IsInitialized() const noexcept { return m_isInitialized; }
        [[nodiscard]] EditorBackend GetBackend() const noexcept { return m_backend; }

    private:
        void ApplyCyberpunkDarkStyle();
        void RenderDiagnosticsOverlay(const Timer& timer, const RHI::IRHIDevice& device, const Scene& scene, u32 renderedEntities);
        void RegisterIniSettingsHandler();
        void BuildDockLayout(WorkspacePreset preset);
        void ApplyPresetPanelStates(WorkspacePreset preset);

    private:
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        LuaConsole m_luaConsole;
        SceneBuilder m_sceneBuilder;
        StressTest m_stressTest;
        Editor::PanelManager m_panelManager;
        Editor::SceneHierarchyPanel* m_hierarchyPanel = nullptr;
        Editor::InspectorPanel* m_inspectorPanel = nullptr;
        Editor::ContentBrowserPanel* m_contentBrowserPanel = nullptr;
        Editor::StatsPanel* m_statsPanel = nullptr;
        EditorBackend m_backend = EditorBackend::DirectX12;
        WorkspacePreset m_activePreset = WorkspacePreset::Default;
        u32 m_dockspaceId = 0;
        bool m_isInitialized = false;
        bool m_showDiagnostics = false;
        bool m_showBlenderPanels = false;
        bool m_dockLayoutInitialized = false;
        bool m_pendingPresetApply = false;
        bool m_hasLoadedPresetFromIni = false;
    };
}
