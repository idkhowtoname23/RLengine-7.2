#include "Scene/Systems/RenderSystem.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"
#include "Graphics/DX12/ShaderCompiler.hpp"
#include "Graphics/DX12/DescriptorManager.hpp"
#include "Graphics/DX12/TextureManager.hpp"
#include "Platform/FileSystem/FileWatcher.hpp"
#include "Platform/FileSystem/VFS.hpp"
#include "Core/Logger.hpp"
#include "Core/Threading/ThreadPool.hpp"
#include "Core/Memory/MemoryStats.hpp"

#include <d3d12.h>
#include <DirectXMath.h>
#include <format>
#include <algorithm>
#include <filesystem>
#include <cmath>

namespace RL
{
    bool FrustumPlanes::TestSphere(const DirectX::XMFLOAT3& center, float radius) const noexcept
    {
        for (u32 i = 0; i < 6; ++i)
        {
            const float dist = Planes[i].x * center.x +
                               Planes[i].y * center.y +
                               Planes[i].z * center.z +
                               Planes[i].w;
            if (dist < -radius)
            {
                return false;
            }
        }
        return true;
    }

    static FrustumPlanes ExtractFrustumPlanes(const DirectX::XMFLOAT4X4& m) noexcept
    {
        FrustumPlanes frustum{};
        frustum.Planes[0] = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
        frustum.Planes[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
        frustum.Planes[2] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
        frustum.Planes[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
        frustum.Planes[4] = { m._13, m._23, m._33, m._43 };
        frustum.Planes[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };

        for (u32 i = 0; i < 6; ++i)
        {
            const float len = std::sqrt(
                frustum.Planes[i].x * frustum.Planes[i].x +
                frustum.Planes[i].y * frustum.Planes[i].y +
                frustum.Planes[i].z * frustum.Planes[i].z);
            if (len > 1e-6f)
            {
                const float inv = 1.0f / len;
                frustum.Planes[i].x *= inv;
                frustum.Planes[i].y *= inv;
                frustum.Planes[i].z *= inv;
                frustum.Planes[i].w *= inv;
            }
        }
        return frustum;
    }

    RenderSystem::~RenderSystem()
    {
        Destroy();
    }

    std::expected<void, std::string> RenderSystem::Initialize(
        ID3D12Device* device,
        DescriptorManager* descriptorManager,
        TextureManager* textureManager,
        ShaderCompiler* shaderCompiler)
    {
        if (!device)
        {
            return std::unexpected("Device pointer is null when initializing RenderSystem.");
        }

        Destroy();

        m_device = device;
        m_instanceCapacity = InitialInstanceCapacity;
        m_descriptorManager = descriptorManager;
        m_textureManager = textureManager;
        m_shaderCompiler = shaderCompiler;

        const Vertex quadVertices[] = {
            { { -0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            { {  0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            { {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
            { { -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
        };

        const u32 quadIndices[] = {
            0, 1, 2,
            0, 2, 3
        };

        auto vbRes = m_vertexBuffer.Initialize(
            device,
            sizeof(quadVertices),
            BufferType::Vertex,
            D3D12_HEAP_TYPE_UPLOAD
        );
        if (!vbRes)
        {
            return std::unexpected(std::format("Failed to allocate Quad VertexBuffer: {}", vbRes.error()));
        }

        auto ibRes = m_indexBuffer.Initialize(
            device,
            sizeof(quadIndices),
            BufferType::Index,
            D3D12_HEAP_TYPE_UPLOAD
        );
        if (!ibRes)
        {
            return std::unexpected(std::format("Failed to allocate Quad IndexBuffer: {}", ibRes.error()));
        }

        std::memcpy(m_vertexBuffer.GetMappedData(), quadVertices, sizeof(quadVertices));
        std::memcpy(m_indexBuffer.GetMappedData(), quadIndices, sizeof(quadIndices));

        m_vbView.BufferLocation = m_vertexBuffer.GetGPUVirtualAddress();
        m_vbView.SizeInBytes = sizeof(quadVertices);
        m_vbView.StrideInBytes = sizeof(Vertex);

        m_ibView.BufferLocation = m_indexBuffer.GetGPUVirtualAddress();
        m_ibView.SizeInBytes = sizeof(quadIndices);
        m_ibView.Format = DXGI_FORMAT_R32_UINT;

        const usize instanceTotalSize = static_cast<usize>(m_instanceCapacity) * sizeof(InstanceData);
        for (u32 i = 0; i < NUM_FRAMES; ++i)
        {
            auto cbRes = m_constantBuffers[i].Initialize(
                device,
                sizeof(ObjectConstants),
                BufferType::Constant,
                D3D12_HEAP_TYPE_UPLOAD
            );
            if (!cbRes)
            {
                return std::unexpected(std::format("Failed to allocate Object ConstantBuffer for frame {}: {}", i, cbRes.error()));
            }

            auto dirCbRes = m_dirShadowConstantBuffers[i].Initialize(
                device,
                sizeof(ObjectConstants),
                BufferType::Constant,
                D3D12_HEAP_TYPE_UPLOAD
            );
            if (!dirCbRes)
            {
                return std::unexpected(std::format("Failed to allocate Directional Shadow ConstantBuffer for frame {}: {}", i, dirCbRes.error()));
            }

            auto ptCbRes = m_pointShadowConstantBuffers[i].Initialize(
                device,
                sizeof(ObjectConstants),
                BufferType::Constant,
                D3D12_HEAP_TYPE_UPLOAD
            );
            if (!ptCbRes)
            {
                return std::unexpected(std::format("Failed to allocate Point Shadow ConstantBuffer for frame {}: {}", i, ptCbRes.error()));
            }

            auto instRes = m_instanceBuffers[i].Initialize(
                device,
                instanceTotalSize,
                BufferType::Generic,
                D3D12_HEAP_TYPE_UPLOAD
            );
            if (!instRes)
            {
                return std::unexpected(std::format("Failed to allocate InstanceBuffer for frame {}: {}", i, instRes.error()));
            }

            auto fcbRes = m_frameConstantBuffers[i].Initialize(
                device,
                sizeof(FrameConstants),
                BufferType::Constant,
                D3D12_HEAP_TYPE_UPLOAD
            );
            if (!fcbRes)
            {
                return std::unexpected(std::format("Failed to allocate Frame ConstantBuffer for frame {}: {}", i, fcbRes.error()));
            }

            auto boneRes = m_boneBuffers[i].Initialize(
                device,
                MaxBoneCount * sizeof(DirectX::XMFLOAT4X4),
                BufferType::Generic,
                D3D12_HEAP_TYPE_UPLOAD
            );
            if (!boneRes)
            {
                return std::unexpected(std::format("Failed to allocate BoneBuffer for frame {}: {}", i, boneRes.error()));
            }

            DirectX::XMFLOAT4X4 identityMatrices[MaxBoneCount];
            for (u32 b = 0; b < MaxBoneCount; ++b)
            {
                DirectX::XMStoreFloat4x4(&identityMatrices[b], DirectX::XMMatrixIdentity());
            }
            std::memcpy(m_boneBuffers[i].GetMappedData(), identityMatrices, sizeof(identityMatrices));

            auto lightRes = m_lightBuffers[i].Initialize(
                device,
                MaxDynamicLights * sizeof(DynamicLightData),
                BufferType::Generic,
                D3D12_HEAP_TYPE_UPLOAD
            );
            if (!lightRes)
            {
                return std::unexpected(std::format("Failed to allocate LightBuffer for frame {}: {}", i, lightRes.error()));
            }
            if (m_lightBuffers[i].GetMappedData())
            {
                std::memset(m_lightBuffers[i].GetMappedData(), 0, MaxDynamicLights * sizeof(DynamicLightData));
            }

            const usize ppCbSize = (sizeof(PostProcessConstants) + 255) & ~static_cast<usize>(255);
            auto ppCbRes = m_postProcessConstantBuffers[i].Initialize(
                device,
                ppCbSize,
                BufferType::Constant,
                D3D12_HEAP_TYPE_UPLOAD
            );
            if (!ppCbRes)
            {
                return std::unexpected(std::format("Failed to allocate PostProcess ConstantBuffer for frame {}: {}", i, ppCbRes.error()));
            }
        }

        D3D12_DESCRIPTOR_HEAP_DESC hdrRtvHeapDesc{};
        hdrRtvHeapDesc.NumDescriptors = 1;
        hdrRtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hdrRtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (SUCCEEDED(device->CreateDescriptorHeap(&hdrRtvHeapDesc, IID_PPV_ARGS(&m_hdrRtvHeap))))
        {
            m_hdrRtv = m_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
        }
        if (m_descriptorManager)
        {
            m_hdrSrvIndex = m_descriptorManager->Allocate();
        }

        for (u32 f = 0; f < FrameCount; ++f)
        {
            for (u32 c = 0; c < ShadowMap::CascadeCount; ++c)
            {
                auto ccbRes = m_cascadeConstantBuffers[f][c].Initialize(
                    device,
                    1024,
                    BufferType::Constant,
                    D3D12_HEAP_TYPE_UPLOAD
                );
                if (!ccbRes)
                {
                    return std::unexpected(std::format("Failed to allocate Cascade ConstantBuffer [{}][{}]: {}", f, c, ccbRes.error()));
                }
            }
        }

        m_trackedInstanceMemory = instanceTotalSize * NUM_FRAMES;
        m_trackedConstantMemory = (sizeof(ObjectConstants) * 3 + sizeof(FrameConstants)) * NUM_FRAMES + (FrameCount * ShadowMap::CascadeCount * 1024);
        Memory::MemoryStats::Track(Memory::MemoryCategory::Graphics, m_trackedInstanceMemory + m_trackedConstantMemory);

        if (m_descriptorManager)
        {
            m_shadowMap = std::make_unique<ShadowMap>();
            auto shadowInitRes = m_shadowMap->Initialize(device, m_descriptorManager);
            if (!shadowInitRes)
            {
                RL_LOG_WARN("ShadowMap initialization warning: {}", shadowInitRes.error());
            }
        }

        auto cubeRes = Mesh::CreateCube(device, 1.0f);
        if (cubeRes) m_cubeMesh = *cubeRes;

        auto sphereRes = Mesh::CreateSphere(device, 0.5f, 48, 32);
        if (sphereRes) m_sphereMesh = *sphereRes;

        auto planeRes = Mesh::CreatePlane(device, 10.0f, 10.0f, 10);
        if (planeRes) m_planeMesh = *planeRes;

        auto gridRes = Mesh::CreateGrid(device, 200.0f, 200);
        if (gridRes) m_gridMesh = *gridRes;

        if (m_cubeMesh) RegisterMesh("Cube", m_cubeMesh);
        if (m_sphereMesh) RegisterMesh("Sphere", m_sphereMesh);
        if (m_planeMesh) RegisterMesh("Plane", m_planeMesh);
        if (m_gridMesh) RegisterMesh("Grid", m_gridMesh);

        if (shaderCompiler)
        {
            auto resolveShader = [](const std::string& rel) -> std::filesystem::path {
                if (rel.find("://") != std::string::npos)
                {
                    if (const auto vfsRes = VFS::Resolve(rel)) return *vfsRes;
                }
                const std::string filename = std::filesystem::path(rel).filename().string();
                if (const auto vfsRes = VFS::Resolve("shaders://" + filename)) return *vfsRes;
                if (const auto vfsRes = VFS::Resolve(rel)) return *vfsRes;
                if (std::filesystem::exists(rel)) return rel;
                wchar_t exePathBuf[MAX_PATH];
                GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
                const std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();
                if (std::filesystem::exists(exeDir / rel)) return exeDir / rel;
                if (std::filesystem::exists(exeDir / "shaders" / filename)) return exeDir / "shaders" / filename;
                if (std::filesystem::exists(exeDir / ".." / rel)) return exeDir / ".." / rel;
                if (std::filesystem::exists(exeDir / ".." / ".." / ".." / rel)) return (exeDir / ".." / ".." / ".." / rel).lexically_normal();
                if (std::filesystem::exists(exeDir / ".." / ".." / ".." / "shaders" / filename)) return (exeDir / ".." / ".." / ".." / "shaders" / filename).lexically_normal();
                if (std::filesystem::exists("../" + rel)) return "../" + rel;
                if (std::filesystem::exists("../../" + rel)) return "../../" + rel;
                return rel;
            };

            const auto particleShaderPath = resolveShader("shaders/particles.hlsl");
            if (std::filesystem::exists(particleShaderPath))
            {
                RL_LOG_INFO("[RenderSystem] Particle shader resolved: '{}'", particleShaderPath.string());
            }

            auto reloadRes = ReloadPipelines();
            if (!reloadRes)
            {
                RL_LOG_WARN("[RenderSystem] Pipeline initialization warning: {}", reloadRes.error());
            }

            std::filesystem::path shadersDir;
            if (const auto vfsRes = VFS::Resolve("shaders://"))
            {
                shadersDir = *vfsRes;
            }
            else
            {
                shadersDir = resolveShader("shaders/mesh3d.hlsl").parent_path();
            }

            if (std::filesystem::exists(shadersDir))
            {
                m_fileWatcher = std::make_unique<FileWatcher>();
                m_fileWatcher->Watch(shadersDir, [this](const FileChangeEvent& evt) {
                    if (evt.type == FileChangeType::Modified || evt.type == FileChangeType::Created)
                    {
                        if (evt.path.extension() == ".hlsl")
                        {
                            RL_LOG_INFO("[RenderSystem] Shader change detected: '{}', queuing reload.", evt.path.filename().string());
                            m_needsShaderReload.store(true, std::memory_order_release);
                        }
                    }
                }, true);

                wchar_t exePathBuf[MAX_PATH];
                GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
                const std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();
                const std::filesystem::path sourceShadersDir = (exeDir / ".." / ".." / ".." / "shaders").lexically_normal();
                if (std::filesystem::exists(sourceShadersDir) && sourceShadersDir != shadersDir)
                {
                    m_fileWatcher->Watch(sourceShadersDir, [this](const FileChangeEvent& evt) {
                        if (evt.type == FileChangeType::Modified || evt.type == FileChangeType::Created)
                        {
                            if (evt.path.extension() == ".hlsl")
                            {
                                RL_LOG_INFO("[RenderSystem] Source shader change detected: '{}', queuing reload.", evt.path.filename().string());
                                m_needsShaderReload.store(true, std::memory_order_release);
                            }
                        }
                    }, true);
                }

                m_fileWatcher->Start();
                RL_LOG_INFO("[RenderSystem] Shader hot-reloading FileWatcher active on '{}'.", shadersDir.string());
            }
        }

        m_activeBatches.reserve(2048);
        m_visibleMeshIndices.reserve(m_instanceCapacity);
        m_visibleQuadIndices.reserve(m_instanceCapacity);
        m_isInitialized = true;
        RL_LOG_INFO("[RenderSystem] Initialized with dynamic instance capacity: {} instances.", m_instanceCapacity);
        return {};
    }

    void RenderSystem::Destroy() noexcept
    {
        if (m_fileWatcher)
        {
            m_fileWatcher->Stop();
            m_fileWatcher.reset();
        }

        m_activeBatches.clear();
        m_activeBatches.shrink_to_fit();
        m_visibleMeshIndices.clear();
        m_visibleMeshIndices.shrink_to_fit();
        m_visibleQuadIndices.clear();
        m_visibleQuadIndices.shrink_to_fit();

        if (m_shadowMap)
        {
            m_shadowMap->Destroy();
            m_shadowMap.reset();
        }

        m_vertexBuffer.Destroy();
        m_indexBuffer.Destroy();

        for (u32 i = 0; i < NUM_FRAMES; ++i)
        {
            m_constantBuffers[i].Destroy();
            m_dirShadowConstantBuffers[i].Destroy();
            m_pointShadowConstantBuffers[i].Destroy();
            m_boneBuffers[i].Destroy();
            m_lightBuffers[i].Destroy();
            m_instanceBuffers[i].Destroy();
            m_frameConstantBuffers[i].Destroy();
            m_postProcessConstantBuffers[i].Destroy();
        }
        DestroyHdrRenderTarget();
        if (m_descriptorManager && m_hdrSrvIndex != DescriptorManager::InvalidIndex)
        {
            m_descriptorManager->Free(m_hdrSrvIndex);
            m_hdrSrvIndex = DescriptorManager::InvalidIndex;
        }
        m_hdrRtvHeap.Reset();
        m_hdrRtv = {};
        m_postProcessPipeline.Reset();
        m_postProcessRootSig.Reset();
        m_meshPipeline = Pipeline{};
        m_wireframePipeline = Pipeline{};
        m_gridPipeline = Pipeline{};
        m_shadowPipeline = Pipeline{};
        m_meshHdrPipeline = Pipeline{};
        m_wireframeHdrPipeline = Pipeline{};
        m_quadHdrPipeline = Pipeline{};
        m_gridHdrPipeline = Pipeline{};

        for (u32 f = 0; f < FrameCount; ++f)
        {
            for (u32 c = 0; c < ShadowMap::CascadeCount; ++c)
            {
                m_cascadeConstantBuffers[f][c].Destroy();
            }
        }

        if (m_trackedInstanceMemory > 0)
        {
            Memory::MemoryStats::Untrack(Memory::MemoryCategory::Graphics, m_trackedInstanceMemory);
            m_trackedInstanceMemory = 0;
        }
        if (m_trackedConstantMemory > 0)
        {
            Memory::MemoryStats::Untrack(Memory::MemoryCategory::Graphics, m_trackedConstantMemory);
            m_trackedConstantMemory = 0;
        }

        m_cubeMesh.reset();
        m_sphereMesh.reset();
        m_planeMesh.reset();
        m_gridMesh.reset();
        m_meshes.clear();

        m_isInitialized = false;
        m_device = nullptr;
        m_instanceCapacity = InitialInstanceCapacity;
        m_lastRenderedCount = 0;
        RL_LOG_INFO("[RenderSystem] RenderSystem GPU resources cleaned up.");
    }

    void RenderSystem::OnResize(u32 width, u32 height)
    {
        if (!m_device || width == 0 || height == 0)
        {
            return;
        }
        if (m_hdrTarget && m_hdrWidth == width && m_hdrHeight == height)
        {
            return;
        }
        CreateHdrRenderTarget(width, height);
    }

    void RenderSystem::DestroyHdrRenderTarget() noexcept
    {
        m_hdrTarget.Reset();
        m_hdrWidth = 0;
        m_hdrHeight = 0;
        m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    bool RenderSystem::CreateHdrRenderTarget(u32 width, u32 height)
    {
        if (!m_device || !m_hdrRtvHeap || width == 0 || height == 0)
        {
            return false;
        }

        DestroyHdrRenderTarget();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Alignment = 0;
        resDesc.Width = width;
        resDesc.Height = height;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearVal{};
        clearVal.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        clearVal.Color[0] = 0.04f;
        clearVal.Color[1] = 0.06f;
        clearVal.Color[2] = 0.09f;
        clearVal.Color[3] = 1.0f;

        HRESULT hr = m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearVal,
            IID_PPV_ARGS(&m_hdrTarget));

        if (FAILED(hr) || !m_hdrTarget)
        {
            RL_LOG_ERROR("[RenderSystem] Failed to create HDR render target ({}x{}).", width, height);
            return false;
        }

        m_hdrRtv = m_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;
        m_device->CreateRenderTargetView(m_hdrTarget.Get(), &rtvDesc, m_hdrRtv);

        if (m_descriptorManager && m_hdrSrvIndex != DescriptorManager::InvalidIndex)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            m_device->CreateShaderResourceView(m_hdrTarget.Get(), &srvDesc, m_descriptorManager->GetCPUHandle(m_hdrSrvIndex));
        }

        m_hdrWidth = width;
        m_hdrHeight = height;
        m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        return true;
    }

    std::expected<void, std::string> RenderSystem::CreatePostProcessPipeline(const std::filesystem::path& shaderPath)
    {
        if (!m_device || !m_shaderCompiler)
        {
            return std::unexpected("Device or ShaderCompiler is null when creating PostProcess pipeline.");
        }

        auto ppVsBlob = m_shaderCompiler->RecompileVertexShader(shaderPath, "VSMain", "6_5");
        if (!ppVsBlob)
        {
            return std::unexpected(std::format("PostProcess VS compilation error: {}", ppVsBlob.error()));
        }
        auto ppPsBlob = m_shaderCompiler->RecompilePixelShader(shaderPath, "PSMain", "6_5");
        if (!ppPsBlob)
        {
            return std::unexpected(std::format("PostProcess PS compilation error: {}", ppPsBlob.error()));
        }

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rootParams[2]{};
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParams[0].Descriptor.ShaderRegister = 0;
        rootParams[0].Descriptor.RegisterSpace = 0;

        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;

        D3D12_STATIC_SAMPLER_DESC staticSampler{};
        staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.MipLODBias = 0.0f;
        staticSampler.MaxAnisotropy = 1;
        staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        staticSampler.MinLOD = 0.0f;
        staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
        staticSampler.ShaderRegister = 0;
        staticSampler.RegisterSpace = 0;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.NumParameters = 2;
        rootSigDesc.pParameters = rootParams;
        rootSigDesc.NumStaticSamplers = 1;
        rootSigDesc.pStaticSamplers = &staticSampler;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sigBlob;
        ComPtr<ID3DBlob> errBlob;
        HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
        if (FAILED(hr))
        {
            const std::string errStr = errBlob ? static_cast<const char*>(errBlob->GetBufferPointer()) : "Unknown error";
            return std::unexpected(std::format("Failed to serialize PostProcess root signature: {}", errStr));
        }

        ComPtr<ID3D12RootSignature> newRootSig;
        hr = m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&newRootSig));
        if (FAILED(hr))
        {
            return std::unexpected("Failed to create PostProcess root signature.");
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = newRootSig.Get();
        psoDesc.VS = { (*ppVsBlob)->GetBufferPointer(), (*ppVsBlob)->GetBufferSize() };
        psoDesc.PS = { (*ppPsBlob)->GetBufferPointer(), (*ppPsBlob)->GetBufferSize() };
        psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
        psoDesc.BlendState.IndependentBlendEnable = FALSE;
        psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
        psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = FALSE;
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.InputLayout = { nullptr, 0 };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleDesc.Quality = 0;

        ComPtr<ID3D12PipelineState> newPso;
        hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&newPso));
        if (FAILED(hr))
        {
            return std::unexpected("Failed to create PostProcess PSO.");
        }

        m_postProcessRootSig = std::move(newRootSig);
        m_postProcessPipeline = std::move(newPso);
        return {};
    }

    void RenderSystem::RegisterMesh(std::string_view name, std::shared_ptr<Mesh> mesh)
    {
        if (mesh)
        {
            m_meshes[std::string(name)] = mesh;
        }
    }

    std::shared_ptr<Mesh> RenderSystem::GetMesh(std::string_view name) const
    {
        auto it = m_meshes.find(std::string(name));
        if (it != m_meshes.end())
        {
            return it->second;
        }
        return m_cubeMesh;
    }

    std::vector<std::string> RenderSystem::GetAvailableMeshNames() const
    {
        std::vector<std::string> names;
        names.reserve(m_meshes.size());
        for (const auto& [name, _] : m_meshes)
        {
            names.push_back(name);
        }
        return names;
    }

    void RenderSystem::EnsureInstanceCapacity(u32 neededCapacity)
    {
        if (neededCapacity <= m_instanceCapacity || !m_device)
        {
            return;
        }

        u32 newCapacity = m_instanceCapacity;
        while (newCapacity < neededCapacity)
        {
            newCapacity *= 2;
        }

        const usize instanceTotalSize = static_cast<usize>(newCapacity) * sizeof(InstanceData);
        for (u32 i = 0; i < NUM_FRAMES; ++i)
        {
            auto instRes = m_instanceBuffers[i].Initialize(
                m_device,
                instanceTotalSize,
                BufferType::Generic,
                D3D12_HEAP_TYPE_UPLOAD
            );
            if (!instRes)
            {
                RL_LOG_ERROR("Failed to grow InstanceBuffer to {} elements: {}", newCapacity, instRes.error());
                return;
            }
        }

        m_instanceCapacity = newCapacity;
        const usize newInstanceTotalSize = instanceTotalSize * NUM_FRAMES;
        if (newInstanceTotalSize > m_trackedInstanceMemory)
        {
            Memory::MemoryStats::Track(Memory::MemoryCategory::Graphics, newInstanceTotalSize - m_trackedInstanceMemory);
        }
        else if (newInstanceTotalSize < m_trackedInstanceMemory)
        {
            Memory::MemoryStats::Untrack(Memory::MemoryCategory::Graphics, m_trackedInstanceMemory - newInstanceTotalSize);
        }
        m_trackedInstanceMemory = newInstanceTotalSize;
        m_visibleMeshIndices.reserve(m_instanceCapacity);
        m_visibleQuadIndices.reserve(m_instanceCapacity);
        RL_LOG_INFO("[RenderSystem] Dynamically grew instance buffers to {} instances ({:.2f} MB per frame).",
            m_instanceCapacity, static_cast<f32>(instanceTotalSize) / (1024.0f * 1024.0f));
    }

    void RenderSystem::PrepareMeshInstances(
        Scene& scene,
        InstanceData* mappedInstances,
        u32& meshInstanceCount,
        const FrustumPlanes& frustum)
    {
        m_activeBatches.clear();
        m_visibleMeshIndices.clear();

        const auto* transformPool = scene.GetPool<TransformComponent>();
        const auto* meshPool = scene.GetPool<MeshComponent>();
        if (!meshPool || !transformPool || !mappedInstances)
        {
            return;
        }

        const auto entities = meshPool->GetEntities();
        const auto meshes = meshPool->GetData();
        const usize totalMeshes = meshes.size();
        if (totalMeshes == 0)
        {
            return;
        }

        const u32 numWorkers = static_cast<u32>(ThreadPool::Get().GetThreadCount());
        const usize minChunkSize = 2048;
        const usize calculatedChunk = (totalMeshes + numWorkers - 1) / (numWorkers > 0 ? numWorkers : 1);
        const usize chunkSize = (std::max)(minChunkSize, calculatedChunk);
        const usize numChunks = (totalMeshes + chunkSize - 1) / chunkSize;

        if (m_chunkVisibleMesh.size() < numChunks)
        {
            m_chunkVisibleMesh.resize(numChunks);
        }
        for (usize c = 0; c < numChunks; ++c)
        {
            m_chunkVisibleMesh[c].clear();
        }

        ThreadPool::Get().ParallelFor(totalMeshes, chunkSize, [&](usize start, usize end) {
            const usize chunkIdx = start / chunkSize;
            auto& visibleList = m_chunkVisibleMesh[chunkIdx];

            for (usize i = start; i < end; ++i)
            {
                if (!meshes[i].IsVisible) continue;
                const u32 entityIdx = entities[i];
                if (!transformPool->Has(entityIdx)) continue;

                const Entity entity = scene.GetEntityFromIndex(entityIdx);
                const DirectX::XMMATRIX world = scene.GetWorldMatrix(entity);
                DirectX::XMFLOAT4X4 worldF;
                DirectX::XMStoreFloat4x4(&worldF, world);

                const DirectX::XMFLOAT3 center{ worldF._41, worldF._42, worldF._43 };
                const float scaleX = std::sqrt(worldF._11 * worldF._11 + worldF._12 * worldF._12 + worldF._13 * worldF._13);
                const float scaleY = std::sqrt(worldF._21 * worldF._21 + worldF._22 * worldF._22 + worldF._23 * worldF._23);
                const float scaleZ = std::sqrt(worldF._31 * worldF._31 + worldF._32 * worldF._32 + worldF._33 * worldF._33);
                const float maxScale = (std::max)({ scaleX, scaleY, scaleZ });
                const float radius = (std::max)(maxScale, 0.001f) * 1.732f;

                if (!frustum.TestSphere(center, radius))
                {
                    continue;
                }

                visibleList.push_back(static_cast<u32>(i));
            }
        });

        usize totalVisibleCount = 0;
        for (usize c = 0; c < numChunks; ++c)
        {
            totalVisibleCount += m_chunkVisibleMesh[c].size();
        }

        m_visibleMeshIndices.reserve(totalVisibleCount);
        for (usize c = 0; c < numChunks; ++c)
        {
            const auto& cv = m_chunkVisibleMesh[c];
            if (meshInstanceCount + m_visibleMeshIndices.size() + cv.size() > m_instanceCapacity)
            {
                const usize allowed = m_instanceCapacity - (meshInstanceCount + m_visibleMeshIndices.size());
                m_visibleMeshIndices.insert(m_visibleMeshIndices.end(), cv.begin(), cv.begin() + allowed);
                break;
            }
            m_visibleMeshIndices.insert(m_visibleMeshIndices.end(), cv.begin(), cv.end());
        }

        if (m_visibleMeshIndices.empty())
        {
            return;
        }

        for (u32 idx : m_visibleMeshIndices)
        {
            if (!meshes[idx].MeshRef)
            {
                auto targetMesh = GetMesh(meshes[idx].MeshName);
                if (!targetMesh) targetMesh = m_cubeMesh;
                const_cast<MeshComponent&>(meshes[idx]).MeshRef = targetMesh;
            }
        }

        std::sort(m_visibleMeshIndices.begin(), m_visibleMeshIndices.end(), [&](u32 a, u32 b) {
            if (meshes[a].Wireframe != meshes[b].Wireframe)
            {
                return meshes[a].Wireframe < meshes[b].Wireframe;
            }
            Mesh* meshA = meshes[a].MeshRef ? meshes[a].MeshRef.get() : m_cubeMesh.get();
            Mesh* meshB = meshes[b].MeshRef ? meshes[b].MeshRef.get() : m_cubeMesh.get();
            if (meshA != meshB)
            {
                return meshA < meshB;
            }
            return a < b;
        });

        const usize totalVisible = m_visibleMeshIndices.size();
        usize current = 0;
        while (current < totalVisible)
        {
            if (meshInstanceCount >= m_instanceCapacity)
            {
                break;
            }

            const bool batchWireframe = meshes[m_visibleMeshIndices[current]].Wireframe;
            Mesh* meshPtr = meshes[m_visibleMeshIndices[current]].MeshRef ? meshes[m_visibleMeshIndices[current]].MeshRef.get() : m_cubeMesh.get();
            usize next = current + 1;
            while (next < totalVisible)
            {
                Mesh* nextMeshPtr = meshes[m_visibleMeshIndices[next]].MeshRef ? meshes[m_visibleMeshIndices[next]].MeshRef.get() : m_cubeMesh.get();
                if (nextMeshPtr != meshPtr || meshes[m_visibleMeshIndices[next]].Wireframe != batchWireframe)
                {
                    break;
                }
                ++next;
            }

            const u32 availableCapacity = m_instanceCapacity - meshInstanceCount;
            const u32 runCount = static_cast<u32>(next - current);
            const u32 batchCount = std::min(runCount, availableCapacity);

            if (batchCount > 0 && meshPtr)
            {
                const u32 startInstance = meshInstanceCount;
                ThreadPool::Get().ParallelFor(batchCount, 2048, [&](usize s, usize e) {
                    for (usize k = s; k < e; ++k)
                    {
                        const u32 idx = m_visibleMeshIndices[current + k];
                        const u32 entityIdx = entities[idx];
                        const Entity entity = scene.GetEntityFromIndex(entityIdx);
                        InstanceData& dst = mappedInstances[startInstance + k];
                        DirectX::XMMATRIX world = scene.GetWorldMatrix(entity);
                        DirectX::XMMATRIX normalMatrix = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, world));
                        DirectX::XMStoreFloat4x4(&dst.WorldMatrix, world);
                        DirectX::XMStoreFloat4x4(&dst.NormalMatrix, normalMatrix);
                        dst.Color = meshes[idx].Color;
                        dst.MaterialParams = { 0.0f, 0.5f, 1.0f, 0.0f };
                        dst.NormalMapIndex = 0;
                        dst.MetallicRoughnessIndex = 0;
                        dst.EmissiveIndex = 0;
                        dst.TextureIndex = meshes[idx].TextureIndex;
                        if (scene.HasComponent<MaterialComponent>(entity))
                        {
                            const auto& mat = scene.GetComponent<MaterialComponent>(entity);
                            dst.MaterialParams = { mat.Metallic, mat.Roughness, mat.AO, 1.0f };
                            dst.Color = mat.AlbedoColor;
                            dst.NormalMapIndex = mat.NormalMapIndex;
                            dst.MetallicRoughnessIndex = mat.MetallicRoughnessMapIndex;
                            dst.EmissiveIndex = mat.EmissiveMapIndex;
                            if (mat.TextureIndex > 0)
                            {
                                dst.TextureIndex = mat.TextureIndex;
                            }
                        }
                    }
                });

                m_activeBatches.push_back(MeshBatch{
                    .MeshPtr = meshPtr,
                    .StartInstance = startInstance,
                    .Count = batchCount,
                    .Wireframe = batchWireframe
                });

                meshInstanceCount += batchCount;
            }

            current = next;
        }
    }

    void RenderSystem::PrepareQuadInstances(
        Scene& scene,
        InstanceData* mappedInstances,
        u32& renderedCount)
    {
        m_visibleQuadIndices.clear();

        const auto* quadPool = scene.GetPool<QuadRenderComponent>();
        const auto* transformPool = scene.GetPool<TransformComponent>();
        if (!quadPool || !transformPool || !m_quadPipeline || !m_quadPipeline->GetPipelineState())
        {
            return;
        }

        const auto entities = quadPool->GetEntities();
        const auto quads = quadPool->GetData();
        const usize totalQuads = quads.size();

        if (totalQuads == 0 || renderedCount >= m_instanceCapacity)
        {
            return;
        }

        const u32 numWorkers = static_cast<u32>(ThreadPool::Get().GetThreadCount());
        const usize minChunkSize = 2048;
        const usize calculatedChunk = (totalQuads + numWorkers - 1) / (numWorkers > 0 ? numWorkers : 1);
        const usize chunkSize = (std::max)(minChunkSize, calculatedChunk);
        const usize numChunks = (totalQuads + chunkSize - 1) / chunkSize;

        if (m_chunkVisibleQuad.size() < numChunks)
        {
            m_chunkVisibleQuad.resize(numChunks);
        }
        for (usize c = 0; c < numChunks; ++c)
        {
            m_chunkVisibleQuad[c].clear();
        }

        ThreadPool::Get().ParallelFor(totalQuads, chunkSize, [&](usize start, usize end) {
            const usize chunkIdx = start / chunkSize;
            auto& visibleList = m_chunkVisibleQuad[chunkIdx];

            for (usize i = start; i < end; ++i)
            {
                if (!quads[i].IsVisible)
                {
                    continue;
                }

                const u32 entityIdx = entities[i];
                if (!transformPool->Has(entityIdx))
                {
                    continue;
                }

                const Entity entity = scene.GetEntityFromIndex(entityIdx);
                const DirectX::XMMATRIX world = scene.GetWorldMatrix(entity);
                DirectX::XMFLOAT4X4 worldF;
                DirectX::XMStoreFloat4x4(&worldF, world);

                const DirectX::XMFLOAT3 center{ worldF._41, worldF._42, worldF._43 };
                const float scaleX = std::sqrt(worldF._11 * worldF._11 + worldF._12 * worldF._12 + worldF._13 * worldF._13);
                const float scaleY = std::sqrt(worldF._21 * worldF._21 + worldF._22 * worldF._22 + worldF._23 * worldF._23);
                const float maxScale = (std::max)(scaleX, scaleY);
                const float radius = 0.7071f * (std::max)(maxScale, 0.001f);

                if (!m_currentFrustum.TestSphere(center, radius))
                {
                    continue;
                }

                visibleList.push_back(static_cast<u32>(i));
            }
        });

        usize totalVisible = 0;
        for (usize c = 0; c < numChunks; ++c)
        {
            totalVisible += m_chunkVisibleQuad[c].size();
        }

        m_visibleQuadIndices.reserve(totalVisible);
        for (usize c = 0; c < numChunks; ++c)
        {
            const auto& cv = m_chunkVisibleQuad[c];
            if (renderedCount + m_visibleQuadIndices.size() + cv.size() > m_instanceCapacity)
            {
                const usize allowed = m_instanceCapacity - (renderedCount + m_visibleQuadIndices.size());
                m_visibleQuadIndices.insert(m_visibleQuadIndices.end(), cv.begin(), cv.begin() + allowed);
                break;
            }
            m_visibleQuadIndices.insert(m_visibleQuadIndices.end(), cv.begin(), cv.end());
        }

        const u32 visibleCount = static_cast<u32>(m_visibleQuadIndices.size());
        if (visibleCount == 0)
        {
            return;
        }

        const u32 startInstance = renderedCount;
        ThreadPool::Get().ParallelFor(visibleCount, 2048, [&](usize s, usize e) {
            for (usize k = s; k < e; ++k)
            {
                const u32 i = m_visibleQuadIndices[k];
                const u32 entityIdx = entities[i];
                const Entity entity = scene.GetEntityFromIndex(entityIdx);
                InstanceData& dst = mappedInstances[startInstance + k];
                dst.MaterialParams = { 0.0f, 0.5f, 1.0f, 0.0f };
                DirectX::XMStoreFloat4x4(&dst.WorldMatrix, scene.GetWorldMatrix(entity));
                DirectX::XMStoreFloat4x4(&dst.NormalMatrix, DirectX::XMMatrixIdentity());
                dst.Color = quads[i].Color;
                dst.TextureIndex = quads[i].TextureIndex;
                dst.NormalMapIndex = 0;
                dst.MetallicRoughnessIndex = 0;
                dst.EmissiveIndex = 0;
            }
        });

        renderedCount += visibleCount;
    }

    void RenderSystem::Render(Scene& scene, ID3D12GraphicsCommandList* cmdList, u32 frameIndex)
    {
        m_transientAllocator.Reset();

        if (m_needsShaderReload.exchange(false, std::memory_order_acq_rel))
        {
            auto reloadRes = ReloadPipelines();
            if (!reloadRes)
            {
                RL_LOG_ERROR("[RenderSystem] Shader hot-reload failed: {}", reloadRes.error());
            }
        }

        if (!m_isInitialized || !cmdList || frameIndex >= NUM_FRAMES)
        {
            return;
        }

        if (m_viewport.Width > 0.0f && m_viewport.Height > 0.0f)
        {
            const u32 vpW = static_cast<u32>(m_viewport.Width);
            const u32 vpH = static_cast<u32>(m_viewport.Height);
            if (!m_hdrTarget || m_hdrWidth != vpW || m_hdrHeight != vpH)
            {
                OnResize(vpW, vpH);
            }
        }

        if (m_descriptorManager)
        {
            m_descriptorManager->Bind(cmdList);
        }

        DirectX::XMFLOAT4X4 viewProj;
        DirectX::XMStoreFloat4x4(&viewProj, scene.GetCamera().GetViewProjectionMatrix());

        m_currentFrustum = ExtractFrustumPlanes(viewProj);
        const FrustumPlanes& cameraFrustum = m_currentFrustum;

        auto* fcbPtr = static_cast<FrameConstants*>(m_frameConstantBuffers[frameIndex].GetMappedData());
        if (fcbPtr)
        {
            fcbPtr->ViewProjection = viewProj;
        }

        auto* passConstants = static_cast<ObjectConstants*>(m_constantBuffers[frameIndex].GetMappedData());
        if (!passConstants)
        {
            return;
        }

        DirectX::XMStoreFloat4x4(&passConstants->WorldMatrix, DirectX::XMMatrixIdentity());
        passConstants->ViewProjection = viewProj;
        passConstants->ColorMultiplier = { 1.0f, 1.0f, 1.0f, 1.0f };

        const auto& cam = scene.GetCamera();
        const auto& camPos = cam.GetPosition();
        passConstants->CameraPos = { camPos.x, camPos.y, camPos.z, 1.0f };

        const auto& env = scene.GetEnvironment();
        const SceneShadingMode shadingMode = scene.GetShadingMode();

        passConstants->SunDirection = { 0.5f, 0.8f, -0.7f, 3.0f };
        passConstants->SunColor = { 1.0f, 0.98f, 0.92f, 1.0f };
        passConstants->AmbientColor = { 0.15f, 0.18f, 0.22f, 1.0f };
        passConstants->PointLightPos = { 0.0f, 4.0f, 0.0f, 15.0f };
        passConstants->PointLightColor = { 1.0f, 0.85f, 0.6f, 5.0f };
        passConstants->PBRParams = { 0.2f, 0.45f, 1.0f, 1.0f };
        passConstants->FogColorDensity = { env.FogColor.x, env.FogColor.y, env.FogColor.z, env.FogDensity };
        passConstants->EnvParams = {
            env.FogEnabled ? 1.0f : 0.0f,
            env.AmbientStrength,
            static_cast<f32>(static_cast<u8>(shadingMode)),
            0.0f
        };

        auto* mappedLights = static_cast<DynamicLightData*>(m_lightBuffers[frameIndex].GetMappedData());
        u32 activeDynamicLightCount = 0;
        i32 primaryShadowLightIdx = -1;
        bool hasExplicitShadowPointLight = false;

        const auto* lightPool = scene.GetPool<LightComponent>();
        const auto* transformPool = scene.GetPool<TransformComponent>();
        if (lightPool && transformPool)
        {
            const auto lights = lightPool->GetData();
            const auto entities = lightPool->GetEntities();
            for (usize i = 0; i < lights.size(); ++i)
            {
                const u32 entityIdx = entities[i];
                const auto* tr = transformPool->TryGet(entityIdx);
                if (!tr) continue;

                const Entity entity = scene.GetEntityFromIndex(entityIdx);
                const DirectX::XMMATRIX worldMat = scene.GetWorldMatrix(entity);
                DirectX::XMFLOAT4X4 worldF{};
                DirectX::XMStoreFloat4x4(&worldF, worldMat);
                const DirectX::XMFLOAT3 worldPos{ worldF._41, worldF._42, worldF._43 };

                if (lights[i].Type == LightType::Directional)
                {
                    DirectX::XMFLOAT3 dir{ 0.5f, 0.8f, -0.7f };
                    if (tr->Position.x != 0.0f || tr->Position.y != 0.0f || tr->Position.z != 0.0f)
                    {
                        dir = tr->Position;
                    }
                    else if (tr->Rotation.x != 0.0f || tr->Rotation.y != 0.0f || tr->Rotation.z != 0.0f)
                    {
                        const DirectX::XMVECTOR baseDir = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                        const DirectX::XMVECTOR rotDir = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(baseDir, worldMat));
                        DirectX::XMStoreFloat3(&dir, rotDir);
                    }
                    passConstants->SunDirection = { dir.x, dir.y, dir.z, lights[i].Intensity };
                    passConstants->SunColor = { lights[i].Color.x, lights[i].Color.y, lights[i].Color.z, 1.0f };
                }
                else if (lights[i].Type == LightType::Point || lights[i].Type == LightType::Spot)
                {
                    if (mappedLights && activeDynamicLightCount < MaxDynamicLights)
                    {
                        DynamicLightData& gpuLight = mappedLights[activeDynamicLightCount];
                        const f32 safeRange = std::max(lights[i].Range, 0.01f);
                        gpuLight.PositionRange = { worldPos.x, worldPos.y, worldPos.z, safeRange };
                        gpuLight.ColorIntensity = { lights[i].Color.x, lights[i].Color.y, lights[i].Color.z, lights[i].Intensity };

                        if (lights[i].Type == LightType::Spot)
                        {
                            const DirectX::XMVECTOR localDown = DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
                            const DirectX::XMVECTOR spotDirVec = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(localDown, worldMat));
                            DirectX::XMFLOAT3 spotDir{ 0.0f, -1.0f, 0.0f };
                            DirectX::XMStoreFloat3(&spotDir, spotDirVec);

                            const f32 halfAngleRad = DirectX::XMConvertToRadians(std::clamp(lights[i].SpotAngle * 0.5f, 1.0f, 89.0f));
                            const f32 outerCos = std::cos(halfAngleRad);
                            const f32 innerCos = std::cos(halfAngleRad * 0.8f);

                            gpuLight.DirectionType = { spotDir.x, spotDir.y, spotDir.z, static_cast<f32>(static_cast<u8>(LightType::Spot)) };
                            gpuLight.SpotParams = { innerCos, outerCos, lights[i].CastShadows ? 1.0f : 0.0f, 0.0f };
                        }
                        else
                        {
                            gpuLight.DirectionType = { 0.0f, -1.0f, 0.0f, static_cast<f32>(static_cast<u8>(LightType::Point)) };
                            gpuLight.SpotParams = { 1.0f, -1.0f, lights[i].CastShadows ? 1.0f : 0.0f, 0.0f };

                            if (!hasExplicitShadowPointLight && (lights[i].CastShadows || primaryShadowLightIdx < 0))
                            {
                                primaryShadowLightIdx = static_cast<i32>(activeDynamicLightCount);
                                hasExplicitShadowPointLight = lights[i].CastShadows;
                                passConstants->PointLightPos = { worldPos.x, worldPos.y, worldPos.z, safeRange };
                                passConstants->PointLightColor = { lights[i].Color.x, lights[i].Color.y, lights[i].Color.z, lights[i].Intensity };
                            }
                        }

                        ++activeDynamicLightCount;
                    }
                }
            }
        }

        if (mappedLights && primaryShadowLightIdx >= 0 && static_cast<u32>(primaryShadowLightIdx) < activeDynamicLightCount)
        {
            mappedLights[primaryShadowLightIdx].SpotParams.w = 1.0f;
        }
        passConstants->EnvParams.w = static_cast<f32>(activeDynamicLightCount);

        auto* mappedBones = static_cast<DirectX::XMFLOAT4X4*>(m_boneBuffers[frameIndex].GetMappedData());
        if (mappedBones)
        {
            u32 copiedBones = 0;
            if (const auto* skinnedPool = scene.GetPool<SkinnedMeshComponent>())
            {
                const auto skinnedMeshes = skinnedPool->GetData();
                for (const auto& sk : skinnedMeshes)
                {
                    if (sk.IsVisible && !sk.BonePalette.empty())
                    {
                        copiedBones = static_cast<u32>(std::min(sk.BonePalette.size(), static_cast<usize>(MaxBoneCount)));
                        std::memcpy(mappedBones, sk.BonePalette.data(), copiedBones * sizeof(DirectX::XMFLOAT4X4));
                        break;
                    }
                }
            }
            for (u32 b = copiedBones; b < MaxBoneCount; ++b)
            {
                DirectX::XMStoreFloat4x4(&mappedBones[b], DirectX::XMMatrixIdentity());
            }
        }

        const f32 cascadeSplits[4] = { 10.0f, 25.0f, 60.0f, 150.0f };
        passConstants->CascadeSplits = { cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], cascadeSplits[3] };

        if (m_shadowMap)
        {
            passConstants->CascadeSrvIndices = {
                static_cast<f32>(m_shadowMap->GetCascadeSrvIndex(0)),
                static_cast<f32>(m_shadowMap->GetCascadeSrvIndex(1)),
                static_cast<f32>(m_shadowMap->GetCascadeSrvIndex(2)),
                static_cast<f32>(m_shadowMap->GetCascadeSrvIndex(3))
            };
            passConstants->CascadeParams = {
                static_cast<f32>(ShadowMap::DirectionalResolution),
                0.0012f,
                static_cast<f32>(ShadowMap::CascadeCount),
                1.0f
            };
            passConstants->ShadowParams = {
                static_cast<f32>(m_shadowMap->GetCascadeSrvIndex(0)),
                0.0015f,
                static_cast<f32>(ShadowMap::DirectionalResolution),
                1.0f
            };
            passConstants->PointShadowParams = {
                static_cast<f32>(m_shadowMap->GetPointSrvIndex()),
                0.003f,
                static_cast<f32>(ShadowMap::PointResolution),
                1.0f
            };
        }
        else
        {
            passConstants->CascadeSrvIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
            passConstants->CascadeParams = { 2048.0f, 0.0012f, 4.0f, 0.0f };
            passConstants->ShadowParams = { 0.0f, 0.0015f, 2048.0f, 0.0f };
            passConstants->PointShadowParams = { 0.0f, 0.003f, 1024.0f, 0.0f };
        }

        DirectX::XMVECTOR sunDirVec = DirectX::XMVector3Normalize(DirectX::XMVectorSet(passConstants->SunDirection.x, passConstants->SunDirection.y, passConstants->SunDirection.z, 0.0f));
        DirectX::XMVECTOR lightUp = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        if (fabsf(DirectX::XMVectorGetY(sunDirVec)) > 0.99f)
        {
            lightUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        }

        const DirectX::XMVECTOR frustumCornersNDC[8] = {
            DirectX::XMVectorSet(-1.0f,  1.0f, 0.0f, 1.0f),
            DirectX::XMVectorSet( 1.0f,  1.0f, 0.0f, 1.0f),
            DirectX::XMVectorSet( 1.0f, -1.0f, 0.0f, 1.0f),
            DirectX::XMVectorSet(-1.0f, -1.0f, 0.0f, 1.0f),
            DirectX::XMVectorSet(-1.0f,  1.0f, 1.0f, 1.0f),
            DirectX::XMVectorSet( 1.0f,  1.0f, 1.0f, 1.0f),
            DirectX::XMVectorSet( 1.0f, -1.0f, 1.0f, 1.0f),
            DirectX::XMVectorSet(-1.0f, -1.0f, 1.0f, 1.0f),
        };

        const DirectX::XMMATRIX lightViewRot = DirectX::XMMatrixLookAtLH(DirectX::XMVectorScale(sunDirVec, 1.0f), DirectX::XMVectorZero(), lightUp);
        const DirectX::XMMATRIX invLightViewRot = DirectX::XMMatrixInverse(nullptr, lightViewRot);
        const f32 shadowMapRes = static_cast<f32>(ShadowMap::DirectionalResolution);

        for (u32 c = 0; c < ShadowMap::CascadeCount; ++c)
        {
            const f32 cascadeNear = (c == 0) ? std::max(cam.GetNearZ(), 0.1f) : cascadeSplits[c - 1];
            const f32 cascadeFar = cascadeSplits[c];

            DirectX::XMMATRIX subProj;
            if (cam.GetProjectionType() == CameraProjection::Perspective)
            {
                subProj = DirectX::XMMatrixPerspectiveFovLH(cam.GetFOV(), cam.GetAspectRatio(), cascadeNear, cascadeFar);
            }
            else
            {
                const f32 safeZoom = (cam.GetZoom() > 0.0001f) ? cam.GetZoom() : 0.0001f;
                const f32 viewHeight = cam.GetOrthoSize() / safeZoom;
                const f32 viewWidth = viewHeight * cam.GetAspectRatio();
                subProj = DirectX::XMMatrixOrthographicLH(viewWidth, viewHeight, cascadeNear, cascadeFar);
            }

            const DirectX::XMMATRIX camView = cam.CalculateViewMatrix();
            const DirectX::XMMATRIX invSubViewProj = DirectX::XMMatrixInverse(nullptr, DirectX::XMMatrixMultiply(camView, subProj));

            DirectX::XMVECTOR corners[8];
            DirectX::XMVECTOR frustumCenter = DirectX::XMVectorZero();
            for (u32 k = 0; k < 8; ++k)
            {
                corners[k] = DirectX::XMVector3TransformCoord(frustumCornersNDC[k], invSubViewProj);
                frustumCenter = DirectX::XMVectorAdd(frustumCenter, corners[k]);
            }
            frustumCenter = DirectX::XMVectorScale(frustumCenter, 1.0f / 8.0f);

            f32 radius = 0.0f;
            for (u32 k = 0; k < 8; ++k)
            {
                const f32 dist = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(corners[k], frustumCenter)));
                radius = std::max(radius, dist);
            }
            radius = std::ceil(radius * 16.0f) / 16.0f;

            const f32 worldUnitsPerTexel = (2.0f * radius) / shadowMapRes;
            const DirectX::XMVECTOR centerLight = DirectX::XMVector3TransformCoord(frustumCenter, lightViewRot);

            f32 cx = DirectX::XMVectorGetX(centerLight);
            f32 cy = DirectX::XMVectorGetY(centerLight);
            const f32 cz = DirectX::XMVectorGetZ(centerLight);

            cx = std::floor(cx / worldUnitsPerTexel) * worldUnitsPerTexel;
            cy = std::floor(cy / worldUnitsPerTexel) * worldUnitsPerTexel;

            const DirectX::XMVECTOR snappedCenterLight = DirectX::XMVectorSet(cx, cy, cz, 1.0f);
            const DirectX::XMVECTOR centerWorld = DirectX::XMVector3TransformCoord(snappedCenterLight, invLightViewRot);

            const DirectX::XMVECTOR lightEye = DirectX::XMVectorAdd(centerWorld, DirectX::XMVectorScale(sunDirVec, radius + 200.0f));
            const DirectX::XMVECTOR lightAt = centerWorld;
            const DirectX::XMMATRIX lightView = DirectX::XMMatrixLookAtLH(lightEye, lightAt, lightUp);

            const DirectX::XMMATRIX lightProj = DirectX::XMMatrixOrthographicOffCenterLH(-radius, radius, -radius, radius, 1.0f, 2.0f * radius + 250.0f);
            const DirectX::XMMATRIX cascadeViewProj = DirectX::XMMatrixMultiply(lightView, lightProj);

            DirectX::XMStoreFloat4x4(&passConstants->CascadeViewProj[c], cascadeViewProj);

            auto* cascadeCB = static_cast<ObjectConstants*>(m_cascadeConstantBuffers[frameIndex][c].GetMappedData());
            if (cascadeCB)
            {
                DirectX::XMStoreFloat4x4(&cascadeCB->WorldMatrix, DirectX::XMMatrixIdentity());
                cascadeCB->ViewProjection = passConstants->CascadeViewProj[c];
            }
        }

        passConstants->LightViewProj = passConstants->CascadeViewProj[0];

        auto* dirShadowCB = static_cast<ObjectConstants*>(m_dirShadowConstantBuffers[frameIndex].GetMappedData());
        if (dirShadowCB)
        {
            DirectX::XMStoreFloat4x4(&dirShadowCB->WorldMatrix, DirectX::XMMatrixIdentity());
            dirShadowCB->ViewProjection = passConstants->CascadeViewProj[0];
        }

        DirectX::XMVECTOR ptEye = DirectX::XMVectorSet(passConstants->PointLightPos.x, passConstants->PointLightPos.y, passConstants->PointLightPos.z, 1.0f);
        DirectX::XMVECTOR ptAt = DirectX::XMVectorAdd(ptEye, DirectX::XMVectorSet(0.0f, -1.0f, 0.01f, 0.0f));
        DirectX::XMMATRIX ptView = DirectX::XMMatrixLookAtLH(ptEye, ptAt, DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        DirectX::XMMATRIX ptProj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(120.0f), 1.0f, 0.1f, passConstants->PointLightPos.w);
        DirectX::XMMATRIX ptViewProj = ptView * ptProj;
        DirectX::XMStoreFloat4x4(&passConstants->PointLightViewProj, ptViewProj);

        auto* ptShadowCB = static_cast<ObjectConstants*>(m_pointShadowConstantBuffers[frameIndex].GetMappedData());
        if (ptShadowCB)
        {
            DirectX::XMStoreFloat4x4(&ptShadowCB->WorldMatrix, DirectX::XMMatrixIdentity());
            ptShadowCB->ViewProjection = passConstants->PointLightViewProj;
        }

        const D3D12_GPU_VIRTUAL_ADDRESS passGpuAddr = m_constantBuffers[frameIndex].GetGPUVirtualAddress();
        const D3D12_GPU_VIRTUAL_ADDRESS ptShadowGpuAddr = m_pointShadowConstantBuffers[frameIndex].GetGPUVirtualAddress();
        const D3D12_GPU_VIRTUAL_ADDRESS boneGpuAddr = m_boneBuffers[frameIndex].GetGPUVirtualAddress();
        const D3D12_GPU_VIRTUAL_ADDRESS lightGpuAddr = m_lightBuffers[frameIndex].GetGPUVirtualAddress();

        u32 potentialInstances = 0;
        if (const auto* meshPool = scene.GetPool<MeshComponent>())
        {
            potentialInstances += static_cast<u32>(meshPool->GetData().size());
        }
        if (const auto* quadPool = scene.GetPool<QuadRenderComponent>())
        {
            potentialInstances += static_cast<u32>(quadPool->GetData().size());
        }
        if (potentialInstances > m_instanceCapacity)
        {
            EnsureInstanceCapacity(potentialInstances);
        }

        const D3D12_GPU_VIRTUAL_ADDRESS instanceGpuAddr = m_instanceBuffers[frameIndex].GetGPUVirtualAddress();
        auto* mappedInstances = static_cast<InstanceData*>(m_instanceBuffers[frameIndex].GetMappedData());

        if (!mappedInstances)
        {
            return;
        }

        if (!transformPool)
        {
            transformPool = scene.GetPool<TransformComponent>();
        }

        u32 meshInstanceCount = 0;
        PrepareMeshInstances(scene, mappedInstances, meshInstanceCount, cameraFrustum);

        const bool usePostProcess = env.PostProcessEnabled &&
            m_hdrTarget &&
            m_hdrRtv.ptr != 0 &&
            m_postProcessRootSig &&
            m_postProcessPipeline &&
            m_descriptorManager &&
            m_hdrSrvIndex != DescriptorManager::InvalidIndex &&
            m_mainRtv.ptr != 0 &&
            m_meshHdrPipeline.GetPipelineState() != nullptr;

        if (shadingMode == SceneShadingMode::Rendered && m_shadowMap && m_shadowPipeline.GetPipelineState() && !m_activeBatches.empty())
        {
            for (u32 cascadeIdx = 0; cascadeIdx < ShadowMap::CascadeCount; ++cascadeIdx)
            {
                m_shadowMap->BeginCascade(cmdList, cascadeIdx);
                m_shadowPipeline.Bind(cmdList);
                cmdList->SetGraphicsRootConstantBufferView(0, m_cascadeConstantBuffers[frameIndex][cascadeIdx].GetGPUVirtualAddress());
                cmdList->SetGraphicsRootShaderResourceView(2, instanceGpuAddr);
                cmdList->SetGraphicsRootShaderResourceView(4, boneGpuAddr);
                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                for (const auto& batch : m_activeBatches)
                {
                    cmdList->SetGraphicsRoot32BitConstant(3, batch.StartInstance, 0);
                    batch.MeshPtr->Bind(cmdList);
                    cmdList->DrawIndexedInstanced(batch.MeshPtr->GetIndexCount(), batch.Count, 0, 0, batch.StartInstance);
                }
                m_shadowMap->EndCascade(cmdList, cascadeIdx);
            }

            m_shadowMap->BeginPoint(cmdList);
            m_shadowPipeline.Bind(cmdList);
            cmdList->SetGraphicsRootConstantBufferView(0, ptShadowGpuAddr);
            cmdList->SetGraphicsRootShaderResourceView(2, instanceGpuAddr);
            cmdList->SetGraphicsRootShaderResourceView(4, boneGpuAddr);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            for (const auto& batch : m_activeBatches)
            {
                cmdList->SetGraphicsRoot32BitConstant(3, batch.StartInstance, 0);
                batch.MeshPtr->Bind(cmdList);
                cmdList->DrawIndexedInstanced(batch.MeshPtr->GetIndexCount(), batch.Count, 0, 0, batch.StartInstance);
            }
            m_shadowMap->EndPoint(cmdList);
        }

        if (usePostProcess)
        {
            if (m_hdrState != D3D12_RESOURCE_STATE_RENDER_TARGET)
            {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = m_hdrTarget.Get();
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = m_hdrState;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                cmdList->ResourceBarrier(1, &barrier);
                m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }
            const float clearColor[4] = { env.SkyColor.x, env.SkyColor.y, env.SkyColor.z, env.SkyColor.w };
            cmdList->ClearRenderTargetView(m_hdrRtv, clearColor, 0, nullptr);
            cmdList->RSSetViewports(1, &m_viewport);
            cmdList->RSSetScissorRects(1, &m_scissorRect);
            cmdList->OMSetRenderTargets(1, &m_hdrRtv, FALSE, (m_mainDsv.ptr != 0) ? &m_mainDsv : nullptr);
        }
        else if (m_mainRtv.ptr != 0 && m_mainDsv.ptr != 0)
        {
            cmdList->RSSetViewports(1, &m_viewport);
            cmdList->RSSetScissorRects(1, &m_scissorRect);
            cmdList->OMSetRenderTargets(1, &m_mainRtv, FALSE, &m_mainDsv);
        }

        u32 renderedCount = 0;

        const Pipeline& activeGridPipeline = usePostProcess ? m_gridHdrPipeline : m_gridPipeline;
        if (m_showGrid && m_gridMesh && activeGridPipeline.GetPipelineState())
        {
            activeGridPipeline.Bind(cmdList);
            cmdList->SetGraphicsRootConstantBufferView(0, passGpuAddr);
            if (m_descriptorManager)
            {
                m_descriptorManager->SetGraphicsRootDescriptorTable(cmdList, 1, 0);
            }
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            m_gridMesh->Bind(cmdList);
            m_gridMesh->Draw(cmdList);
        }

        const Pipeline& activeMeshPipeline = usePostProcess ? m_meshHdrPipeline : m_meshPipeline;
        const Pipeline& activeWireframePipeline = usePostProcess ? m_wireframeHdrPipeline : m_wireframePipeline;
        if (!m_activeBatches.empty() && activeMeshPipeline.GetPipelineState())
        {
            bool hasBoundMeshPipeline = false;
            bool boundWireframe = false;

            for (const auto& batch : m_activeBatches)
            {
                const bool useWireframe = ((shadingMode == SceneShadingMode::Wireframe) || batch.Wireframe) &&
                                          (activeWireframePipeline.GetPipelineState() != nullptr);

                if (!hasBoundMeshPipeline || useWireframe != boundWireframe)
                {
                    if (useWireframe)
                    {
                        activeWireframePipeline.Bind(cmdList);
                    }
                    else
                    {
                        activeMeshPipeline.Bind(cmdList);
                    }
                    cmdList->SetGraphicsRootConstantBufferView(0, passGpuAddr);
                    if (m_descriptorManager)
                    {
                        m_descriptorManager->SetGraphicsRootDescriptorTable(cmdList, 1, 0);
                    }
                    cmdList->SetGraphicsRootShaderResourceView(2, instanceGpuAddr);
                    cmdList->SetGraphicsRootShaderResourceView(4, boneGpuAddr);
                    cmdList->SetGraphicsRootShaderResourceView(5, lightGpuAddr);
                    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                    hasBoundMeshPipeline = true;
                    boundWireframe = useWireframe;
                }

                cmdList->SetGraphicsRoot32BitConstant(3, batch.StartInstance, 0);
                batch.MeshPtr->Bind(cmdList);
                cmdList->DrawIndexedInstanced(batch.MeshPtr->GetIndexCount(), batch.Count, 0, 0, batch.StartInstance);
            }
        }

        renderedCount += meshInstanceCount;

        const Pipeline* activeQuadPipeline = usePostProcess ? &m_quadHdrPipeline : m_quadPipeline;
        const auto* quadPool = scene.GetPool<QuadRenderComponent>();
        if (quadPool && transformPool && activeQuadPipeline && activeQuadPipeline->GetPipelineState())
        {
            const auto quads = quadPool->GetData();
            const usize totalQuads = quads.size();

            if (totalQuads > 0 && renderedCount < m_instanceCapacity)
            {
                const u32 startInstance = renderedCount;

                PrepareQuadInstances(scene, mappedInstances, renderedCount);

                const u32 quadCount = renderedCount - startInstance;
                if (quadCount > 0)
                {
                    activeQuadPipeline->Bind(cmdList);
                    cmdList->SetGraphicsRootConstantBufferView(0, passGpuAddr);
                    if (m_descriptorManager)
                    {
                        m_descriptorManager->SetGraphicsRootDescriptorTable(cmdList, 1, 0);
                    }
                    cmdList->SetGraphicsRootShaderResourceView(2, instanceGpuAddr);
                    cmdList->SetGraphicsRootShaderResourceView(4, boneGpuAddr);
                    cmdList->SetGraphicsRoot32BitConstant(3, startInstance, 0);
                    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    cmdList->IASetVertexBuffers(0, 1, &m_vbView);
                    cmdList->IASetIndexBuffer(&m_ibView);
                    cmdList->DrawIndexedInstanced(6, quadCount, 0, 0, 0);
                }
            }
        }

        if (usePostProcess)
        {
            if (m_hdrState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = m_hdrTarget.Get();
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = m_hdrState;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                cmdList->ResourceBarrier(1, &barrier);
                m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }

            cmdList->RSSetViewports(1, &m_viewport);
            cmdList->RSSetScissorRects(1, &m_scissorRect);
            cmdList->OMSetRenderTargets(1, &m_mainRtv, FALSE, nullptr);

            if (auto* ppCb = static_cast<PostProcessConstants*>(m_postProcessConstantBuffers[frameIndex].GetMappedData()))
            {
                ppCb->Exposure = env.Exposure;
                ppCb->BloomThreshold = env.BloomThreshold;
                ppCb->BloomIntensity = env.BloomIntensity;
                ppCb->VignetteIntensity = env.VignetteIntensity;
                ppCb->TexelSize = {
                    1.0f / static_cast<f32>(std::max(m_hdrWidth, 1u)),
                    1.0f / static_cast<f32>(std::max(m_hdrHeight, 1u))
                };
                ppCb->Pad = { 0.0f, 0.0f };
            }

            cmdList->SetGraphicsRootSignature(m_postProcessRootSig.Get());
            cmdList->SetPipelineState(m_postProcessPipeline.Get());
            cmdList->SetGraphicsRootConstantBufferView(0, m_postProcessConstantBuffers[frameIndex].GetGPUVirtualAddress());
            m_descriptorManager->SetGraphicsRootDescriptorTable(cmdList, 1, m_hdrSrvIndex);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->DrawInstanced(3, 1, 0, 0);

            cmdList->OMSetRenderTargets(1, &m_mainRtv, FALSE, (m_mainDsv.ptr != 0) ? &m_mainDsv : nullptr);
        }

        m_lastRenderedCount = renderedCount;
    }

    std::expected<void, std::string> RenderSystem::ReloadPipelines()
    {
        if (!m_device)
        {
            return std::unexpected("Device pointer is null when reloading pipelines.");
        }
        if (!m_shaderCompiler)
        {
            return std::unexpected("ShaderCompiler pointer is null when reloading pipelines.");
        }

        auto resolveShader = [](const std::string& rel) -> std::filesystem::path {
            if (rel.find("://") != std::string::npos)
            {
                if (const auto vfsRes = VFS::Resolve(rel)) return *vfsRes;
            }
            const std::string filename = std::filesystem::path(rel).filename().string();
            if (const auto vfsRes = VFS::Resolve("shaders://" + filename)) return *vfsRes;
            if (const auto vfsRes = VFS::Resolve(rel)) return *vfsRes;
            if (std::filesystem::exists(rel)) return rel;
            wchar_t exePathBuf[MAX_PATH];
            GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
            const std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();
            if (std::filesystem::exists(exeDir / rel)) return exeDir / rel;
            if (std::filesystem::exists(exeDir / "shaders" / filename)) return exeDir / "shaders" / filename;
            if (std::filesystem::exists(exeDir / ".." / rel)) return exeDir / ".." / rel;
            if (std::filesystem::exists(exeDir / ".." / ".." / ".." / rel)) return (exeDir / ".." / ".." / ".." / rel).lexically_normal();
            if (std::filesystem::exists(exeDir / ".." / ".." / ".." / "shaders" / filename)) return (exeDir / ".." / ".." / ".." / "shaders" / filename).lexically_normal();
            if (std::filesystem::exists("../" + rel)) return "../" + rel;
            if (std::filesystem::exists("../../" + rel)) return "../../" + rel;
            return rel;
        };

        m_shaderCompiler->ClearCache();

        const auto meshShaderPath = resolveShader("shaders/mesh3d.hlsl");
        auto meshVsBlob = m_shaderCompiler->RecompileVertexShader(meshShaderPath, "VSMain", "6_5");
        if (!meshVsBlob)
        {
            return std::unexpected(std::format("Mesh VS compilation error: {}", meshVsBlob.error()));
        }
        auto meshPsBlob = m_shaderCompiler->RecompilePixelShader(meshShaderPath, "PSMain", "6_5");
        if (!meshPsBlob)
        {
            return std::unexpected(std::format("Mesh PS compilation error: {}", meshPsBlob.error()));
        }

        const D3D12_INPUT_ELEMENT_DESC meshLayout[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(MeshVertex, Position),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(MeshVertex, Normal),      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(MeshVertex, UV),          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(MeshVertex, Color),       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(MeshVertex, Tangent),     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, offsetof(MeshVertex, BoneIndices), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(MeshVertex, BoneWeights), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        PipelineConfig meshCfg{};
        meshCfg.TopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        meshCfg.CullMode = D3D12_CULL_MODE_BACK;
        meshCfg.FillMode = D3D12_FILL_MODE_SOLID;
        meshCfg.RtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        meshCfg.EnableBlend = false;
        meshCfg.EnableDepth = true;
        meshCfg.DepthWrite = true;
        meshCfg.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        meshCfg.DsvFormat = DXGI_FORMAT_D32_FLOAT;

        Pipeline newMeshPipeline;
        auto initMeshRes = newMeshPipeline.Initialize(m_device, (*meshVsBlob).Get(), (*meshPsBlob).Get(), meshLayout, meshCfg);
        if (!initMeshRes)
        {
            return std::unexpected(std::format("Mesh pipeline creation failed: {}", initMeshRes.error()));
        }

        PipelineConfig meshHdrCfg = meshCfg;
        meshHdrCfg.RtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        Pipeline newMeshHdrPipeline;
        auto initMeshHdrRes = newMeshHdrPipeline.Initialize(m_device, (*meshVsBlob).Get(), (*meshPsBlob).Get(), meshLayout, meshHdrCfg);
        if (!initMeshHdrRes)
        {
            return std::unexpected(std::format("Mesh HDR pipeline creation failed: {}", initMeshHdrRes.error()));
        }

        PipelineConfig wireframeCfg = meshCfg;
        wireframeCfg.FillMode = D3D12_FILL_MODE_WIREFRAME;
        wireframeCfg.CullMode = D3D12_CULL_MODE_NONE;

        Pipeline newWireframePipeline;
        auto initWireframeRes = newWireframePipeline.Initialize(m_device, (*meshVsBlob).Get(), (*meshPsBlob).Get(), meshLayout, wireframeCfg);
        if (!initWireframeRes)
        {
            return std::unexpected(std::format("Wireframe pipeline creation failed: {}", initWireframeRes.error()));
        }

        PipelineConfig wireframeHdrCfg = wireframeCfg;
        wireframeHdrCfg.RtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        Pipeline newWireframeHdrPipeline;
        auto initWireframeHdrRes = newWireframeHdrPipeline.Initialize(m_device, (*meshVsBlob).Get(), (*meshPsBlob).Get(), meshLayout, wireframeHdrCfg);
        if (!initWireframeHdrRes)
        {
            return std::unexpected(std::format("Wireframe HDR pipeline creation failed: {}", initWireframeHdrRes.error()));
        }

        const auto quadShaderPath = resolveShader("shaders/quad.hlsl");
        auto quadVsBlob = m_shaderCompiler->RecompileVertexShader(quadShaderPath, "VSMain", "6_5");
        if (!quadVsBlob)
        {
            return std::unexpected(std::format("Quad VS compilation error: {}", quadVsBlob.error()));
        }
        auto quadPsBlob = m_shaderCompiler->RecompilePixelShader(quadShaderPath, "PSMain", "6_5");
        if (!quadPsBlob)
        {
            return std::unexpected(std::format("Quad PS compilation error: {}", quadPsBlob.error()));
        }

        const D3D12_INPUT_ELEMENT_DESC quadLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(Vertex, Position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, Color),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(Vertex, UV),       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        PipelineConfig quadCfg{};
        quadCfg.TopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        quadCfg.CullMode = D3D12_CULL_MODE_NONE;
        quadCfg.FillMode = D3D12_FILL_MODE_SOLID;
        quadCfg.RtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        quadCfg.EnableBlend = true;
        quadCfg.EnableDepth = true;
        quadCfg.DepthWrite = false;
        quadCfg.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        quadCfg.DsvFormat = DXGI_FORMAT_D32_FLOAT;

        Pipeline newQuadPipeline;
        auto initQuadRes = newQuadPipeline.Initialize(m_device, (*quadVsBlob).Get(), (*quadPsBlob).Get(), quadLayout, quadCfg);
        if (!initQuadRes)
        {
            return std::unexpected(std::format("Quad pipeline creation failed: {}", initQuadRes.error()));
        }

        PipelineConfig quadHdrCfg = quadCfg;
        quadHdrCfg.RtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        Pipeline newQuadHdrPipeline;
        auto initQuadHdrRes = newQuadHdrPipeline.Initialize(m_device, (*quadVsBlob).Get(), (*quadPsBlob).Get(), quadLayout, quadHdrCfg);
        if (!initQuadHdrRes)
        {
            return std::unexpected(std::format("Quad HDR pipeline creation failed: {}", initQuadHdrRes.error()));
        }

        const auto shadowShaderPath = resolveShader("shaders/shadow_depth.hlsl");
        auto shadowVsBlob = m_shaderCompiler->RecompileVertexShader(shadowShaderPath, "VSMain", "6_5");
        if (!shadowVsBlob)
        {
            return std::unexpected(std::format("Shadow VS compilation error: {}", shadowVsBlob.error()));
        }

        PipelineConfig shadowCfg{};
        shadowCfg.TopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        shadowCfg.CullMode = D3D12_CULL_MODE_NONE;
        shadowCfg.FillMode = D3D12_FILL_MODE_SOLID;
        shadowCfg.NumRenderTargets = 0;
        shadowCfg.RtvFormat = DXGI_FORMAT_UNKNOWN;
        shadowCfg.EnableDepth = true;
        shadowCfg.DepthWrite = true;
        shadowCfg.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        shadowCfg.DsvFormat = DXGI_FORMAT_D32_FLOAT;
        shadowCfg.DepthBias = 50;
        shadowCfg.SlopeScaledDepthBias = 1.0f;

        Pipeline newShadowPipeline;
        auto initShadowRes = newShadowPipeline.Initialize(m_device, (*shadowVsBlob).Get(), nullptr, meshLayout, shadowCfg);
        if (!initShadowRes)
        {
            return std::unexpected(std::format("Shadow pipeline creation failed: {}", initShadowRes.error()));
        }

        const auto gridShaderPath = resolveShader("shaders/grid3d.hlsl");
        auto gridVsBlob = m_shaderCompiler->RecompileVertexShader(gridShaderPath, "VSMain", "6_5");
        if (!gridVsBlob)
        {
            return std::unexpected(std::format("Grid VS compilation error: {}", gridVsBlob.error()));
        }
        auto gridPsBlob = m_shaderCompiler->RecompilePixelShader(gridShaderPath, "PSMain", "6_5");
        if (!gridPsBlob)
        {
            return std::unexpected(std::format("Grid PS compilation error: {}", gridPsBlob.error()));
        }

        PipelineConfig gridCfg{};
        gridCfg.TopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        gridCfg.CullMode = D3D12_CULL_MODE_NONE;
        gridCfg.FillMode = D3D12_FILL_MODE_SOLID;
        gridCfg.RtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        gridCfg.EnableBlend = true;
        gridCfg.EnableDepth = true;
        gridCfg.DepthWrite = false;
        gridCfg.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        gridCfg.DsvFormat = DXGI_FORMAT_D32_FLOAT;

        Pipeline newGridPipeline;
        auto initGridRes = newGridPipeline.Initialize(m_device, (*gridVsBlob).Get(), (*gridPsBlob).Get(), meshLayout, gridCfg);
        if (!initGridRes)
        {
            return std::unexpected(std::format("Grid pipeline creation failed: {}", initGridRes.error()));
        }

        PipelineConfig gridHdrCfg = gridCfg;
        gridHdrCfg.RtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        Pipeline newGridHdrPipeline;
        auto initGridHdrRes = newGridHdrPipeline.Initialize(m_device, (*gridVsBlob).Get(), (*gridPsBlob).Get(), meshLayout, gridHdrCfg);
        if (!initGridHdrRes)
        {
            return std::unexpected(std::format("Grid HDR pipeline creation failed: {}", initGridHdrRes.error()));
        }

        const auto postProcessShaderPath = resolveShader("shaders/postprocess.hlsl");
        auto ppRes = CreatePostProcessPipeline(postProcessShaderPath);
        if (!ppRes)
        {
            RL_LOG_WARN("[RenderSystem] PostProcess pipeline warning: {}", ppRes.error());
        }

        m_meshPipeline = std::move(newMeshPipeline);
        m_wireframePipeline = std::move(newWireframePipeline);
        m_ownedQuadPipeline = std::move(newQuadPipeline);
        m_quadPipeline = &m_ownedQuadPipeline;
        m_shadowPipeline = std::move(newShadowPipeline);
        m_gridPipeline = std::move(newGridPipeline);
        m_meshHdrPipeline = std::move(newMeshHdrPipeline);
        m_wireframeHdrPipeline = std::move(newWireframeHdrPipeline);
        m_quadHdrPipeline = std::move(newQuadHdrPipeline);
        m_gridHdrPipeline = std::move(newGridHdrPipeline);

        RL_LOG_INFO("[RenderSystem] All 5 graphics pipelines reloaded successfully.");
        return {};
    }
}
