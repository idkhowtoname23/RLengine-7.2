#include "Editor/EditorLayer.hpp"
#include "Editor/UI/Theme.hpp"
#include "Graphics/DX12/DX12Device.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Camera.hpp"
#include "Core/Timer.hpp"
#include "Core/Logger.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_dx12.h>
#include <cstdio>
#include <cstring>
#include <format>

namespace RL
{
    EditorLayer::~EditorLayer()
    {
        Shutdown();
    }

    std::expected<void, std::string> EditorLayer::Initialize(
        HWND hwnd,
        ID3D12Device* device,
        u32 numFrames,
        DXGI_FORMAT rtvFormat,
        ScriptEngine* scriptEngine)
    {
        if (m_isInitialized)
        {
            return {};
        }

        if (!hwnd || !device)
        {
            return std::unexpected("Invalid HWND or ID3D12Device passed to EditorLayer::Initialize.");
        }

        D3D12_DESCRIPTOR_HEAP_DESC descHeap{};
        descHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        descHeap.NumDescriptors = 16;
        descHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        descHeap.NodeMask = 0;

        HRESULT hr = device->CreateDescriptorHeap(&descHeap, IID_PPV_ARGS(&m_srvHeap));
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create ImGui descriptor heap. HRESULT: 0x{:08X}", static_cast<u32>(hr)));
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        RegisterIniSettingsHandler();
        Editor::Theme::Apply(Editor::EditorTheme::BlenderDark);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = "imgui.ini";

        if (!ImGui_ImplWin32_Init(hwnd))
        {
            return std::unexpected("ImGui_ImplWin32_Init failed.");
        }

        const auto cpuHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        const auto gpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

        if (!ImGui_ImplDX12_Init(device, static_cast<int>(numFrames), rtvFormat, m_srvHeap.Get(), cpuHandle, gpuHandle))
        {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            return std::unexpected("ImGui_ImplDX12_Init failed.");
        }

        m_luaConsole.SetScriptEngine(scriptEngine);
        m_sceneBuilder.SetEditorLayer(this);

        m_hierarchyPanel = m_panelManager.AddPanel<Editor::SceneHierarchyPanel>(nullptr);
        m_inspectorPanel = m_panelManager.AddPanel<Editor::InspectorPanel>(nullptr);
        m_contentBrowserPanel = m_panelManager.AddPanel<Editor::ContentBrowserPanel>();
        m_statsPanel = m_panelManager.AddPanel<Editor::StatsPanel>();

        m_hierarchyPanel->SetSelectionCallback([this](Entity ent) {
            if (m_inspectorPanel)
            {
                m_inspectorPanel->SetSelected(ent.IsValid() ? std::make_optional(ent) : std::nullopt);
            }
            m_sceneBuilder.SelectEntity(ent);
        });

        Logger::SetLogCallback([this](LogLevel level, std::string_view message) {
            m_luaConsole.AddLog(std::string(message), level, level >= LogLevel::Error);
        });

        m_backend = EditorBackend::DirectX12;
        m_isInitialized = true;
        RL_LOG_INFO("[EditorLayer] Dear ImGui (v{}) with DX12 backend initialized successfully.", IMGUI_VERSION);
        return {};
    }

    std::expected<void, std::string> EditorLayer::InitializeDX11(
        HWND hwnd,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ScriptEngine* scriptEngine)
    {
        if (m_isInitialized)
        {
            return {};
        }

        if (!hwnd || !device || !context)
        {
            return std::unexpected("Invalid HWND, ID3D11Device, or ID3D11DeviceContext passed to EditorLayer::InitializeDX11.");
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        RegisterIniSettingsHandler();
        Editor::Theme::Apply(Editor::EditorTheme::BlenderDark);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = "imgui.ini";

        if (!ImGui_ImplWin32_Init(hwnd))
        {
            ImGui::DestroyContext();
            return std::unexpected("ImGui_ImplWin32_Init failed.");
        }

        if (!ImGui_ImplDX11_Init(device, context))
        {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            return std::unexpected("ImGui_ImplDX11_Init failed.");
        }

        m_luaConsole.SetScriptEngine(scriptEngine);
        m_sceneBuilder.SetEditorLayer(this);

        m_hierarchyPanel = m_panelManager.AddPanel<Editor::SceneHierarchyPanel>(nullptr);
        m_inspectorPanel = m_panelManager.AddPanel<Editor::InspectorPanel>(nullptr);
        m_contentBrowserPanel = m_panelManager.AddPanel<Editor::ContentBrowserPanel>();
        m_statsPanel = m_panelManager.AddPanel<Editor::StatsPanel>();

        m_hierarchyPanel->SetSelectionCallback([this](Entity ent) {
            if (m_inspectorPanel)
            {
                m_inspectorPanel->SetSelected(ent.IsValid() ? std::make_optional(ent) : std::nullopt);
            }
            m_sceneBuilder.SelectEntity(ent);
        });

        Logger::SetLogCallback([this](LogLevel level, std::string_view message) {
            m_luaConsole.AddLog(std::string(message), level, level >= LogLevel::Error);
        });

        m_backend = EditorBackend::DirectX11;
        m_isInitialized = true;
        RL_LOG_INFO("[EditorLayer] Dear ImGui (v{}) with DX11 backend initialized successfully.", IMGUI_VERSION);
        return {};
    }

    void EditorLayer::Shutdown() noexcept
    {
        if (!m_isInitialized)
        {
            return;
        }

        Logger::RemoveLogCallback();

        if (m_backend == EditorBackend::DirectX12)
        {
            ImGui_ImplDX12_Shutdown();
        }
        else
        {
            ImGui_ImplDX11_Shutdown();
        }
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        m_srvHeap.Reset();
        m_isInitialized = false;

        RL_LOG_INFO("[EditorLayer] Dear ImGui subsystem shut down cleanly.");
    }

    void EditorLayer::BeginFrame()
    {
        if (!m_isInitialized)
        {
            return;
        }

        if (m_backend == EditorBackend::DirectX12)
        {
            ImGui_ImplDX12_NewFrame();
        }
        else
        {
            ImGui_ImplDX11_NewFrame();
        }
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false) || ImGui::IsKeyPressed(ImGuiKey_F1, false))
        {
            m_luaConsole.Toggle();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
        {
            m_sceneBuilder.ToggleOpen();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
        {
            m_showBlenderPanels = !m_showBlenderPanels;
        }
    }

    void EditorLayer::RenderUI(
        const Timer& timer,
        const RHI::IRHIDevice& device,
        Scene& scene,
        u32 renderedEntities,
        RenderSystem* renderSystem,
        TextureManager* textureManager,
        ScriptEngine* scriptEngine)
    {
        if (!m_isInitialized)
        {
            return;
        }

        m_sceneBuilder.SetEditorLayer(this);
        if (auto* handler = ImGui::FindSettingsHandler("RLWorkspace"))
        {
            handler->UserData = this;
        }

        const ImGuiID dockspaceId = ImHashStr("RLMainDockSpace");
        m_dockspaceId = dockspaceId;

        if (!m_dockLayoutInitialized)
        {
            m_dockLayoutInitialized = true;
            if (!m_hasLoadedPresetFromIni || ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
            {
                ApplyPresetPanelStates(m_activePreset);
                BuildDockLayout(m_activePreset);
            }
        }

        if (m_pendingPresetApply)
        {
            m_pendingPresetApply = false;
            BuildDockLayout(m_activePreset);
            ImGui::MarkIniSettingsDirty();
            if (ImGui::GetIO().IniFilename)
            {
                ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
            }
        }

        ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        m_sceneBuilder.Render(scene, device, timer, renderSystem, textureManager, scriptEngine, m_stressTest);

        if (m_showBlenderPanels)
        {
            if (m_hierarchyPanel)
            {
                m_hierarchyPanel->SetScene(&scene);
                const Entity sel = m_sceneBuilder.GetSelectedEntity();
                m_hierarchyPanel->SetSelected(sel.IsValid() ? std::make_optional(sel) : std::nullopt);
            }
            if (m_inspectorPanel)
            {
                m_inspectorPanel->SetScene(&scene);
                const Entity sel = m_sceneBuilder.GetSelectedEntity();
                m_inspectorPanel->SetSelected(sel.IsValid() ? std::make_optional(sel) : std::nullopt);
            }
            if (m_statsPanel)
            {
                m_statsPanel->SetStats(
                    timer.GetFPS(),
                    timer.GetDeltaTime() * 1000.0f,
                    scene.GetAliveEntityCount(),
                    static_cast<u64>(renderedEntities),
                    1u,
                    static_cast<u64>(renderedEntities * 12)
                );
            }
            m_panelManager.RenderAll();
        }

        RenderDiagnosticsOverlay(timer, device, scene, renderedEntities);

        m_luaConsole.Render();
    }

    void EditorLayer::EndFrame(ID3D12GraphicsCommandList* cmdList)
    {
        if (!m_isInitialized || !cmdList)
        {
            return;
        }

        ImGui::Render();

        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, heaps);

        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
    }

    void EditorLayer::EndFrameDX11()
    {
        if (!m_isInitialized)
        {
            return;
        }

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    void EditorLayer::ApplyCyberpunkDarkStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding    = 8.0f;
        style.FrameRounding     = 5.0f;
        style.PopupRounding     = 6.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding      = 4.0f;
        style.TabRounding       = 6.0f;
        style.WindowBorderSize  = 1.0f;
        style.FrameBorderSize   = 1.0f;
        style.WindowPadding     = ImVec2(10.0f, 10.0f);
        style.FramePadding      = ImVec2(8.0f, 4.0f);
        style.ItemSpacing       = ImVec2(8.0f, 6.0f);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.94f, 0.98f, 1.00f);
        colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.55f, 0.65f, 1.00f);
        colors[ImGuiCol_WindowBg]              = ImVec4(0.06f, 0.08f, 0.12f, 0.92f);
        colors[ImGuiCol_ChildBg]               = ImVec4(0.04f, 0.05f, 0.08f, 0.90f);
        colors[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.10f, 0.15f, 0.96f);
        colors[ImGuiCol_Border]                = ImVec4(0.18f, 0.25f, 0.38f, 0.70f);
        colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_FrameBg]               = ImVec4(0.10f, 0.14f, 0.20f, 0.85f);
        colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.15f, 0.22f, 0.32f, 1.00f);
        colors[ImGuiCol_FrameBgActive]         = ImVec4(0.20f, 0.30f, 0.44f, 1.00f);

        colors[ImGuiCol_TitleBg]               = ImVec4(0.08f, 0.10f, 0.16f, 1.00f);
        colors[ImGuiCol_TitleBgActive]         = ImVec4(0.12f, 0.18f, 0.28f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.04f, 0.05f, 0.08f, 0.75f);

        colors[ImGuiCol_MenuBarBg]             = ImVec4(0.08f, 0.10f, 0.15f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.04f, 0.05f, 0.08f, 0.60f);
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.20f, 0.28f, 0.40f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.25f, 0.38f, 0.55f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.30f, 0.45f, 0.65f, 1.00f);

        colors[ImGuiCol_CheckMark]             = ImVec4(0.25f, 0.95f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab]            = ImVec4(0.25f, 0.85f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.40f, 0.95f, 1.00f, 1.00f);

        colors[ImGuiCol_Button]                = ImVec4(0.14f, 0.22f, 0.35f, 0.85f);
        colors[ImGuiCol_ButtonHovered]         = ImVec4(0.20f, 0.32f, 0.50f, 1.00f);
        colors[ImGuiCol_ButtonActive]          = ImVec4(0.16f, 0.48f, 0.75f, 1.00f);

        colors[ImGuiCol_Header]                = ImVec4(0.15f, 0.24f, 0.36f, 0.80f);
        colors[ImGuiCol_HeaderHovered]         = ImVec4(0.22f, 0.35f, 0.52f, 1.00f);
        colors[ImGuiCol_HeaderActive]          = ImVec4(0.20f, 0.42f, 0.65f, 1.00f);

        colors[ImGuiCol_Separator]             = ImVec4(0.18f, 0.25f, 0.38f, 0.70f);
        colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.25f, 0.45f, 0.70f, 1.00f);
        colors[ImGuiCol_SeparatorActive]       = ImVec4(0.30f, 0.60f, 0.90f, 1.00f);
    }

    void EditorLayer::RenderDiagnosticsOverlay(
        const Timer& timer,
        const RHI::IRHIDevice& device,
        const Scene& scene,
        u32 renderedEntities)
    {
        if (!m_showDiagnostics)
        {
            return;
        }

        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav;

        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);

        if (ImGui::Begin("##DiagnosticsOverlay", &m_showDiagnostics, windowFlags))
        {
            ImGui::TextColored(ImVec4(0.25f, 0.95f, 1.00f, 1.0f), "RLengine 7.2 [DX11/DX12 + Lua 5.4 | Blender Editor]");
            ImGui::Separator();

            ImGui::Text("GPU: %.*s", static_cast<int>(device.GetGpuDescription().size()), device.GetGpuDescription().data());
            ImGui::Text("FPS: %u (%.2f ms)", timer.GetFPS(), timer.GetFrameTimeMS());
            ImGui::Text("ECS Entities: %zu (Rendered: %u)", scene.GetAliveEntityCount(), renderedEntities);

            const auto& cam = scene.GetCamera();
            const auto& camPos = cam.GetPosition();
            ImGui::Text("Camera Pos: (%.2f, %.2f) | Zoom: %.2fx", camPos.x, camPos.y, cam.GetZoom());

            ImGui::Separator();
            ImGui::TextDisabled("Console: [~] or [F1] | Editor: [F2] | Extra Panels: [F3]");
            if (ImGui::SmallButton(m_luaConsole.IsOpen() ? "Hide Console" : "Console"))
            {
                m_luaConsole.Toggle();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(m_sceneBuilder.IsOpen() ? "Hide Editor" : "Editor (F2)"))
            {
                m_sceneBuilder.ToggleOpen();
            }

            ImGui::End();
        }
    }

    void EditorLayer::RegisterIniSettingsHandler()
    {
        if (ImGui::FindSettingsHandler("RLWorkspace") != nullptr)
        {
            ImGui::RemoveSettingsHandler("RLWorkspace");
        }

        ImGuiSettingsHandler handler{};
        handler.TypeName = "RLWorkspace";
        handler.TypeHash = ImHashStr("RLWorkspace");
        handler.UserData = this;

        handler.ClearAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h) {
            if (auto* self = static_cast<EditorLayer*>(h->UserData))
            {
                self->m_activePreset = WorkspacePreset::Default;
                self->m_hasLoadedPresetFromIni = false;
            }
        };

        handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* h, const char* name) -> void* {
            if (name && std::strcmp(name, "Preset") == 0)
            {
                return h->UserData;
            }
            return nullptr;
        };

        handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
            auto* self = static_cast<EditorLayer*>(entry);
            if (!self || !line)
            {
                return;
            }
            int val = 0;
            if (std::sscanf(line, "ActivePreset=%d", &val) == 1)
            {
                if (val >= 0 && val <= static_cast<int>(WorkspacePreset::Debug))
                {
                    self->m_activePreset = static_cast<WorkspacePreset>(val);
                    self->m_hasLoadedPresetFromIni = true;
                }
            }
        };

        handler.ApplyAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h) {
            if (auto* self = static_cast<EditorLayer*>(h->UserData))
            {
                if (self->m_hasLoadedPresetFromIni)
                {
                    self->ApplyPresetPanelStates(self->m_activePreset);
                }
            }
        };

        handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* outBuf) {
            const auto* self = static_cast<const EditorLayer*>(h->UserData);
            if (!self || !outBuf)
            {
                return;
            }
            outBuf->appendf("[%s][Preset]\n", h->TypeName);
            outBuf->appendf("ActivePreset=%d\n\n", static_cast<int>(self->m_activePreset));
        };

        ImGui::AddSettingsHandler(&handler);
    }

    void EditorLayer::ApplyPresetPanelStates(WorkspacePreset preset)
    {
        m_showBlenderPanels = true;

        const bool showHierarchy = (preset == WorkspacePreset::Default || preset == WorkspacePreset::Debug);
        const bool showInspector = (preset != WorkspacePreset::Debug);
        const bool showContentBrowser = (preset == WorkspacePreset::Default || preset == WorkspacePreset::Animation || preset == WorkspacePreset::Scripting);
        const bool showStats = (preset == WorkspacePreset::Debug);
        const bool showLuaConsole = (preset == WorkspacePreset::Scripting);

        if (m_hierarchyPanel) m_hierarchyPanel->SetVisible(showHierarchy);
        if (m_inspectorPanel) m_inspectorPanel->SetVisible(showInspector);
        if (m_contentBrowserPanel) m_contentBrowserPanel->SetVisible(showContentBrowser);
        if (m_statsPanel) m_statsPanel->SetVisible(showStats);
        m_luaConsole.SetOpen(showLuaConsole);

        m_sceneBuilder.SetHierarchyVisible(false);
        m_sceneBuilder.SetInspectorVisible(false);

        switch (preset)
        {
            case WorkspacePreset::Modeling:
                m_sceneBuilder.SetWorkspace(BlenderWorkspace::Modeling);
                m_sceneBuilder.SetTimelineVisible(false);
                m_sceneBuilder.SetToolShelfVisible(true);
                break;
            case WorkspacePreset::Animation:
                m_sceneBuilder.SetWorkspace(BlenderWorkspace::Animation);
                m_sceneBuilder.SetTimelineVisible(true);
                m_sceneBuilder.SetToolShelfVisible(true);
                break;
            case WorkspacePreset::Scripting:
                m_sceneBuilder.SetWorkspace(BlenderWorkspace::Layout);
                m_sceneBuilder.SetTimelineVisible(false);
                m_sceneBuilder.SetToolShelfVisible(false);
                break;
            case WorkspacePreset::Debug:
                m_sceneBuilder.SetWorkspace(BlenderWorkspace::Layout);
                m_sceneBuilder.SetTimelineVisible(false);
                m_sceneBuilder.SetToolShelfVisible(false);
                break;
            case WorkspacePreset::Default:
            default:
                m_sceneBuilder.SetWorkspace(BlenderWorkspace::Layout);
                m_sceneBuilder.SetTimelineVisible(true);
                m_sceneBuilder.SetToolShelfVisible(true);
                break;
        }
    }

    void EditorLayer::BuildDockLayout(WorkspacePreset preset)
    {
        const ImGuiID dockMain = (m_dockspaceId != 0) ? m_dockspaceId : ImHashStr("RLMainDockSpace");
        m_dockspaceId = dockMain;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 workPos = vp ? vp->WorkPos : ImVec2(0.0f, 0.0f);
        const ImVec2 workSize = (vp && vp->WorkSize.x > 0.0f && vp->WorkSize.y > 0.0f)
            ? vp->WorkSize
            : ImVec2(1600.0f, 900.0f);

        ImGui::DockBuilderRemoveNode(dockMain);
        ImGui::DockBuilderAddNode(dockMain, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodePos(dockMain, workPos);
        ImGui::DockBuilderSetNodeSize(dockMain, workSize);

        ImGuiID dockCenter = dockMain;

        switch (preset)
        {
            case WorkspacePreset::Modeling:
            {
                ImGuiID dockRight = 0;
                ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, 0.16f, &dockRight, &dockCenter);
                ImGui::DockBuilderDockWindow("Inspector##INS", dockRight);
                ImGui::DockBuilderDockWindow("Properties", dockRight);
                break;
            }
            case WorkspacePreset::Animation:
            {
                ImGuiID dockRight = 0;
                ImGuiID dockBottom = 0;
                ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, 0.20f, &dockRight, &dockCenter);
                ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.26f, &dockBottom, &dockCenter);
                ImGui::DockBuilderDockWindow("Inspector##INS", dockRight);
                ImGui::DockBuilderDockWindow("Properties", dockRight);
                ImGui::DockBuilderDockWindow("Content Browser##CB", dockBottom);
                ImGui::DockBuilderDockWindow("##BlenderTimeline", dockBottom);
                break;
            }
            case WorkspacePreset::Scripting:
            {
                ImGuiID dockRight = 0;
                ImGuiID dockBottom = 0;
                ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, 0.22f, &dockRight, &dockCenter);
                ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.35f, &dockBottom, &dockCenter);
                ImGui::DockBuilderDockWindow("Inspector##INS", dockRight);
                ImGui::DockBuilderDockWindow("Properties", dockRight);
                ImGui::DockBuilderDockWindow("RLengine 7.2 - In-Game Lua REPL Console [~ or F1]", dockBottom);
                ImGui::DockBuilderDockWindow("Content Browser##CB", dockBottom);
                break;
            }
            case WorkspacePreset::Debug:
            {
                ImGuiID dockLeft = 0;
                ImGuiID dockRight = 0;
                ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Left, 0.22f, &dockLeft, &dockCenter);
                ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, 0.25f, &dockRight, &dockCenter);
                ImGui::DockBuilderDockWindow("Stats##STATS", dockLeft);
                ImGui::DockBuilderDockWindow("Scene Hierarchy##SH", dockRight);
                ImGui::DockBuilderDockWindow("Outliner", dockRight);
                break;
            }
            case WorkspacePreset::Default:
            default:
            {
                ImGuiID dockRight = 0;
                ImGuiID dockOutliner = 0;
                ImGuiID dockInspector = 0;
                ImGuiID dockBottom = 0;
                ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, 0.22f, &dockRight, &dockCenter);
                ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up, 0.42f, &dockOutliner, &dockInspector);
                ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.24f, &dockBottom, &dockCenter);
                ImGui::DockBuilderDockWindow("Scene Hierarchy##SH", dockOutliner);
                ImGui::DockBuilderDockWindow("Outliner", dockOutliner);
                ImGui::DockBuilderDockWindow("Inspector##INS", dockInspector);
                ImGui::DockBuilderDockWindow("Properties", dockInspector);
                ImGui::DockBuilderDockWindow("Content Browser##CB", dockBottom);
                break;
            }
        }

        ImGui::DockBuilderFinish(dockMain);
    }

    void EditorLayer::ApplyWorkspacePreset(WorkspacePreset preset)
    {
        m_activePreset = preset;
        m_dockLayoutInitialized = true;
        ApplyPresetPanelStates(preset);

        if (!m_isInitialized || !ImGui::GetCurrentContext())
        {
            m_pendingPresetApply = true;
            return;
        }

        const ImGuiID dockspaceId = (m_dockspaceId != 0) ? m_dockspaceId : ImHashStr("RLMainDockSpace");
        m_dockspaceId = dockspaceId;

        if (GImGui && GImGui->WithinFrameScope)
        {
            m_pendingPresetApply = true;
            ImGui::MarkIniSettingsDirty();
        }
        else
        {
            BuildDockLayout(preset);
            ImGui::MarkIniSettingsDirty();
            if (ImGui::GetIO().IniFilename)
            {
                ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
            }
        }
    }
}
