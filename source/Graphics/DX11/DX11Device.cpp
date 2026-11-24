#include "Graphics/DX11/DX11Device.hpp"
#include "Graphics/DX12/Mesh.hpp"
#include "Graphics/RHI/GpuCapabilities.hpp"
#include "Assets/AssetImporters.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"
#include "Platform/FileSystem/VFS.hpp"
#include "Core/Logger.hpp"
#include "Core/HResult.hpp"

#include <d3dcompiler.h>
#include <stb_image.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <vector>

namespace RL
{
    struct CBLayout
    {
        DirectX::XMFLOAT4X4 WorldViewProj;
        DirectX::XMFLOAT4X4 World;
        DirectX::XMFLOAT4X4 LightViewProj;
        DirectX::XMFLOAT4 ObjectColor;
        DirectX::XMFLOAT4 LightDir;
        DirectX::XMFLOAT4 LightColor;
        DirectX::XMFLOAT4 AmbientColor;
        DirectX::XMFLOAT4 CameraPos;
        DirectX::XMFLOAT4 FogParams;
        DirectX::XMFLOAT4 PointLightPos;
        DirectX::XMFLOAT4 PointLightColor;
        DirectX::XMFLOAT4 SpotLightPos;
        DirectX::XMFLOAT4 SpotLightDir;
        DirectX::XMFLOAT4 SpotLightColor;
        DirectX::XMFLOAT4 MaterialParams;
    };

    namespace
    {
        [[nodiscard]] std::vector<DX11Vertex> ConvertMeshVertices(std::span<const MeshVertex> src)
        {
            std::vector<DX11Vertex> dst;
            dst.reserve(src.size());
            for (const auto& v : src)
            {
                dst.push_back(DX11Vertex{
                    { v.Position.x, v.Position.y, v.Position.z },
                    { v.Normal.x, v.Normal.y, v.Normal.z },
                    { v.UV.x, v.UV.y },
                    { v.Color.x, v.Color.y, v.Color.z, v.Color.w },
                    { v.Tangent.x, v.Tangent.y, v.Tangent.z, v.Tangent.w }
                });
            }
            return dst;
        }

        [[nodiscard]] std::string CanonicalPrimitiveName(std::string_view name)
        {
            std::string lower(name);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (lower == "cube") return "Cube";
            if (lower == "sphere") return "Sphere";
            if (lower == "plane") return "Plane";
            if (lower == "grid") return "Grid";
            if (lower == "quad") return "Quad";
            return std::string(name);
        }
    }

    void DX11CommandBuffer::SetViewport(f32 x, f32 y, f32 width, f32 height)
    {
        if (!m_context) return;
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = x;
        vp.TopLeftY = y;
        vp.Width = width;
        vp.Height = height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);
    }

    void DX11CommandBuffer::SetScissorRect(i32 left, i32 top, i32 right, i32 bottom)
    {
        if (!m_context) return;
        D3D11_RECT rect{ left, top, right, bottom };
        m_context->RSSetScissorRects(1, &rect);
    }

    void DX11CommandBuffer::SetPipelineState(RHI::IRHIPipelineState*)
    {
    }

    void DX11CommandBuffer::SetConstantBuffer(u32, RHI::IRHIBuffer*)
    {
    }

    void DX11CommandBuffer::SetVertexBuffer(RHI::IRHIBuffer*, u32, u32)
    {
    }

    void DX11CommandBuffer::SetIndexBuffer(RHI::IRHIBuffer*, u32)
    {
    }

    void DX11CommandBuffer::DrawIndexed(u32 indexCount, u32 startIndex, i32 baseVertex)
    {
        if (!m_context) return;
        m_context->DrawIndexed(indexCount, startIndex, baseVertex);
    }

    void DX11CommandBuffer::DrawIndexedInstanced(u32 indexCountPerInstance, u32 instanceCount, u32 startIndex, i32 baseVertex, u32 startInstance)
    {
        if (!m_context) return;
        m_context->DrawIndexedInstanced(indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance);
    }

    DX11Device::DX11Device()
    {
        m_commandBuffer = std::make_unique<DX11CommandBuffer>(nullptr);
    }

    DX11Device::~DX11Device()
    {
        Shutdown();
    }

    std::expected<void, std::string> DX11Device::Initialize(HWND hwnd, u32 width, u32 height)
    {
        m_width = width;
        m_height = height;

        RL_LOG_INFO("Initializing DirectX 11 graphics subsystem (Legacy & Multi-backend support)...");

        auto devRes = CreateDeviceAndSwapChain(hwnd, width, height);
        if (!devRes) return devRes;

        m_gpuInfo = RHI::GpuCapabilities::DetectPrimaryGpu();
        if (m_gpuInfo.name.empty() || m_gpuInfo.name == "Unknown Video Controller")
        {
            m_gpuInfo.name = m_gpuDescription;
        }
        m_gpuInfo.dedicatedVramBytes = m_dedicatedVramBytes;

        if (m_commandBuffer)
        {
            m_commandBuffer->SetContext(m_context.Get());
        }

        auto rtvRes = CreateRenderTargetAndDepthViews();
        if (!rtvRes) return rtvRes;

        auto shadowRes = CreateShadowResources();
        if (!shadowRes)
        {
            RL_LOG_WARN("DX11 shadow resources creation warning: {}", shadowRes.error());
        }

        auto texRes = CreateDefaultTextureAndSampler();
        if (!texRes)
        {
            RL_LOG_WARN("DX11 default texture creation warning: {}", texRes.error());
        }

        auto shaderRes = InitBasicShaders();
        if (!shaderRes)
        {
            RL_LOG_WARN("DX11 basic shaders initialization warning: {}", shaderRes.error());
        }

        auto meshRes = InitProceduralMeshes();
        if (!meshRes)
        {
            RL_LOG_WARN("DX11 procedural mesh cache initialization warning: {}", meshRes.error());
        }

        RL_LOG_INFO("DirectX 11 subsystem initialized successfully on '{}' [Feature Level 11_0+, VRAM: {:.2f} GB]",
                    m_gpuDescription,
                    static_cast<f64>(m_dedicatedVramBytes) / (1024.0 * 1024.0 * 1024.0));
        return {};
    }

    void DX11Device::Shutdown() noexcept
    {
        if (m_context)
        {
            m_context->ClearState();
        }

        m_meshCache.clear();
        m_textureCache.clear();
        m_fallbackSRV.Reset();
        m_fallbackTexture.Reset();
        m_fallbackNormalSRV.Reset();
        m_fallbackNormalTexture.Reset();
        m_linearSampler.Reset();

        m_shadowVS.Reset();
        m_shadowSampler.Reset();
        m_shadowSRV.Reset();
        m_shadowDSV.Reset();
        m_shadowTexture.Reset();

        m_gridInputLayout.Reset();
        m_gridPS.Reset();
        m_gridVS.Reset();
        m_gridVertexBuffer.Reset();
        m_gridVertexCount = 0;

        m_cubeIndexBuffer.Reset();
        m_cubeVertexBuffer.Reset();
        m_constantBuffer.Reset();
        m_inputLayout.Reset();
        m_ps.Reset();
        m_vs.Reset();

        m_alphaBlendState.Reset();
        m_wireframeRasterState.Reset();
        m_rasterState.Reset();
        m_depthState.Reset();
        m_dsv.Reset();
        m_depthBuffer.Reset();
        m_rtv.Reset();
        m_swapChain.Reset();
        m_context.Reset();
        m_device.Reset();

        RL_LOG_INFO("DirectX 11 subsystem shut down cleanly.");
    }

    void DX11Device::Resize(u32 width, u32 height)
    {
        if (!m_swapChain || width == 0 || height == 0) return;

        m_width = width;
        m_height = height;

        if (m_context)
        {
            m_context->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
            m_context->PSSetShaderResources(0, 3, nullSRVs);
        }

        m_rtv.Reset();
        m_depthBuffer.Reset();
        m_dsv.Reset();

        HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
        {
            RL_LOG_ERROR("DX11Device: failed to resize swap chain buffers. HRESULT: 0x{:08X}", static_cast<u32>(hr));
            return;
        }

        auto rtvRes = CreateRenderTargetAndDepthViews();
        if (!rtvRes)
        {
            RL_LOG_ERROR("DX11Device: failed to recreate views after resize: {}", rtvRes.error());
        }
    }

    void DX11Device::BeginFrame()
    {
        if (!m_context || !m_rtv) return;

        ID3D11RenderTargetView* rtvs[] = { m_rtv.Get() };
        m_context->OMSetRenderTargets(1, rtvs, m_dsv.Get());
        if (m_depthState)
        {
            m_context->OMSetDepthStencilState(m_depthState.Get(), 1);
        }
        if (m_rasterState)
        {
            m_context->RSSetState(m_rasterState.Get());
        }

        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<f32>(m_width);
        vp.Height = static_cast<f32>(m_height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        m_context->RSSetViewports(1, &vp);
    }

    void DX11Device::ClearRenderTarget(f32 r, f32 g, f32 b, f32 a)
    {
        if (!m_context || !m_rtv) return;
        const float clearColor[4] = { r, g, b, a };
        m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
        if (m_dsv)
        {
            m_context->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        }
    }

    void DX11Device::ClearRenderTarget(const DirectX::XMFLOAT4& color)
    {
        ClearRenderTarget(color.x, color.y, color.z, color.w);
    }

    void DX11Device::EndFrame()
    {
    }

    void DX11Device::Present(bool vsync)
    {
        if (!m_swapChain) return;
        m_swapChain->Present(vsync ? 1 : 0, 0);
    }

    void DX11Device::WaitForGPU()
    {
        if (m_context)
        {
            m_context->Flush();
        }
    }

    bool DX11Device::HasCachedMesh(std::string_view key) const
    {
        return m_meshCache.find(std::string(key)) != m_meshCache.end();
    }

    u32 DX11Device::GetCachedMeshIndexCount(std::string_view key) const
    {
        const auto it = m_meshCache.find(std::string(key));
        return (it != m_meshCache.end()) ? it->second.indexCount : 0u;
    }

    std::expected<void, std::string> DX11Device::CreateDeviceAndSwapChain(HWND hwnd, u32 width, u32 height)
    {
        DXGI_SWAP_CHAIN_DESC scd{};
        scd.BufferCount = 2;
        scd.BufferDesc.Width = width;
        scd.BufferDesc.Height = height;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferDesc.RefreshRate.Numerator = 60;
        scd.BufferDesc.RefreshRate.Denominator = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hwnd;
        scd.SampleDesc.Count = 1;
        scd.SampleDesc.Quality = 0;
        scd.Windowed = TRUE;
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT createDeviceFlags = 0;
#if defined(_DEBUG)
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        const D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createDeviceFlags,
            featureLevels,
            static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION,
            &scd,
            &m_swapChain,
            &m_device,
            &m_featureLevel,
            &m_context
        );

        if (FAILED(hr))
        {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                createDeviceFlags,
                featureLevels,
                static_cast<UINT>(std::size(featureLevels)),
                D3D11_SDK_VERSION,
                &scd,
                &m_swapChain,
                &m_device,
                &m_featureLevel,
                &m_context
            );
        }

        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 Device and SwapChain: 0x{:08X}", static_cast<u32>(hr)));
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        if (SUCCEEDED(m_device.As(&dxgiDevice)))
        {
            ComPtr<IDXGIAdapter> adapter;
            if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
            {
                DXGI_ADAPTER_DESC desc;
                if (SUCCEEDED(adapter->GetDesc(&desc)))
                {
                    char descStr[128];
                    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, descStr, sizeof(descStr), nullptr, nullptr);
                    m_gpuDescription = descStr;
                    m_dedicatedVramBytes = desc.DedicatedVideoMemory;
                }
            }
        }

        return {};
    }

    std::expected<void, std::string> DX11Device::CreateRenderTargetAndDepthViews()
    {
        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to get back buffer from DX11 swap chain: 0x{:08X}", static_cast<u32>(hr)));
        }

        hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 RenderTargetView: 0x{:08X}", static_cast<u32>(hr)));
        }

        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = m_width;
        depthDesc.Height = m_height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.SampleDesc.Quality = 0;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        hr = m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthBuffer);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 Depth Buffer: 0x{:08X}", static_cast<u32>(hr)));
        }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = depthDesc.Format;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        hr = m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc, &m_dsv);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 DepthStencilView: 0x{:08X}", static_cast<u32>(hr)));
        }

        D3D11_DEPTH_STENCIL_DESC dsDesc{};
        dsDesc.DepthEnable = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

        hr = m_device->CreateDepthStencilState(&dsDesc, &m_depthState);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 DepthStencilState: 0x{:08X}", static_cast<u32>(hr)));
        }

        D3D11_RASTERIZER_DESC rastDesc{};
        rastDesc.FillMode = D3D11_FILL_SOLID;
        rastDesc.CullMode = D3D11_CULL_NONE;
        rastDesc.FrontCounterClockwise = FALSE;
        rastDesc.DepthClipEnable = TRUE;

        hr = m_device->CreateRasterizerState(&rastDesc, &m_rasterState);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 RasterizerState: 0x{:08X}", static_cast<u32>(hr)));
        }

        D3D11_RASTERIZER_DESC wireDesc{};
        wireDesc.FillMode = D3D11_FILL_WIREFRAME;
        wireDesc.CullMode = D3D11_CULL_NONE;
        wireDesc.FrontCounterClockwise = FALSE;
        wireDesc.DepthClipEnable = TRUE;

        hr = m_device->CreateRasterizerState(&wireDesc, &m_wireframeRasterState);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 Wireframe RasterizerState: 0x{:08X}", static_cast<u32>(hr)));
        }

        D3D11_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        hr = m_device->CreateBlendState(&blendDesc, &m_alphaBlendState);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 Alpha BlendState: 0x{:08X}", static_cast<u32>(hr)));
        }

        return {};
    }

    std::expected<void, std::string> DX11Device::CreateShadowResources()
    {
        D3D11_TEXTURE2D_DESC shadowDesc{};
        shadowDesc.Width = 2048;
        shadowDesc.Height = 2048;
        shadowDesc.MipLevels = 1;
        shadowDesc.ArraySize = 1;
        shadowDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        shadowDesc.SampleDesc.Count = 1;
        shadowDesc.SampleDesc.Quality = 0;
        shadowDesc.Usage = D3D11_USAGE_DEFAULT;
        shadowDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&shadowDesc, nullptr, &m_shadowTexture);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 shadow texture: 0x{:08X}", static_cast<u32>(hr)));
        }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        hr = m_device->CreateDepthStencilView(m_shadowTexture.Get(), &dsvDesc, &m_shadowDSV);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 shadow DSV: 0x{:08X}", static_cast<u32>(hr)));
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        hr = m_device->CreateShaderResourceView(m_shadowTexture.Get(), &srvDesc, &m_shadowSRV);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 shadow SRV: 0x{:08X}", static_cast<u32>(hr)));
        }

        D3D11_SAMPLER_DESC shadowSampDesc{};
        shadowSampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        shadowSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        shadowSampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        shadowSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        shadowSampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        shadowSampDesc.MinLOD = 0.0f;
        shadowSampDesc.MaxLOD = D3D11_FLOAT32_MAX;

        hr = m_device->CreateSamplerState(&shadowSampDesc, &m_shadowSampler);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 shadow sampler: 0x{:08X}", static_cast<u32>(hr)));
        }

        return {};
    }

    std::expected<void, std::string> DX11Device::CreateDefaultTextureAndSampler()
    {
        D3D11_SAMPLER_DESC sampDesc{};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MinLOD = 0.0f;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

        HRESULT hr = m_device->CreateSamplerState(&sampDesc, &m_linearSampler);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 linear sampler: 0x{:08X}", static_cast<u32>(hr)));
        }

        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width = 1;
        texDesc.Height = 1;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage = D3D11_USAGE_IMMUTABLE;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        const u32 whitePixel = 0xFFFFFFFF;
        D3D11_SUBRESOURCE_DATA initData{};
        initData.pSysMem = &whitePixel;
        initData.SysMemPitch = sizeof(u32);

        hr = m_device->CreateTexture2D(&texDesc, &initData, &m_fallbackTexture);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 fallback texture: 0x{:08X}", static_cast<u32>(hr)));
        }

        hr = m_device->CreateShaderResourceView(m_fallbackTexture.Get(), nullptr, &m_fallbackSRV);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 fallback SRV: 0x{:08X}", static_cast<u32>(hr)));
        }

        const u32 flatNormalPixel = 0xFFFF8080;
        D3D11_SUBRESOURCE_DATA normalInitData{};
        normalInitData.pSysMem = &flatNormalPixel;
        normalInitData.SysMemPitch = sizeof(u32);

        hr = m_device->CreateTexture2D(&texDesc, &normalInitData, &m_fallbackNormalTexture);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 fallback normal texture: 0x{:08X}", static_cast<u32>(hr)));
        }

        hr = m_device->CreateShaderResourceView(m_fallbackNormalTexture.Get(), nullptr, &m_fallbackNormalSRV);
        if (FAILED(hr))
        {
            return std::unexpected(std::format("Failed to create DX11 fallback normal SRV: 0x{:08X}", static_cast<u32>(hr)));
        }

        return {};
    }

    ID3D11ShaderResourceView* DX11Device::GetOrCreateTexture(const std::string& path)
    {
        if (path.empty())
        {
            return m_fallbackSRV.Get();
        }

        auto it = m_textureCache.find(path);
        if (it != m_textureCache.end())
        {
            return it->second.Get();
        }

        std::filesystem::path resolvedPath = path;
        if (auto vfsResolved = VFS::Resolve(path); vfsResolved && std::filesystem::exists(*vfsResolved))
        {
            resolvedPath = *vfsResolved;
        }
        else if (auto assetsResolved = VFS::Resolve("assets://" + path); assetsResolved && std::filesystem::exists(*assetsResolved))
        {
            resolvedPath = *assetsResolved;
        }
        else if (!std::filesystem::exists(resolvedPath))
        {
            return m_fallbackSRV.Get();
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(resolvedPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels)
        {
            return m_fallbackSRV.Get();
        }

        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width = static_cast<UINT>(width);
        texDesc.Height = static_cast<UINT>(height);
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage = D3D11_USAGE_IMMUTABLE;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData{};
        initData.pSysMem = pixels;
        initData.SysMemPitch = static_cast<UINT>(width * 4);

        ComPtr<ID3D11Texture2D> tex;
        HRESULT hr = m_device->CreateTexture2D(&texDesc, &initData, &tex);
        stbi_image_free(pixels);

        if (FAILED(hr))
        {
            return m_fallbackSRV.Get();
        }

        ComPtr<ID3D11ShaderResourceView> srv;
        hr = m_device->CreateShaderResourceView(tex.Get(), nullptr, &srv);
        if (FAILED(hr))
        {
            return m_fallbackSRV.Get();
        }

        auto* result = srv.Get();
        m_textureCache.emplace(path, std::move(srv));
        return result;
    }

    const DX11MeshBuffers* DX11Device::UploadMeshData(
        std::string_view key,
        std::span<const DX11Vertex> vertices,
        std::span<const u32> indices,
        bool isLineList)
    {
        if (!m_device || vertices.empty())
        {
            return nullptr;
        }

        std::vector<u32> generatedIndices;
        if (indices.empty())
        {
            generatedIndices.resize(vertices.size());
            for (usize i = 0; i < vertices.size(); ++i)
            {
                generatedIndices[i] = static_cast<u32>(i);
            }
            indices = generatedIndices;
        }

        DX11MeshBuffers buffers{};
        buffers.indexCount = static_cast<u32>(indices.size());
        buffers.isLineList = isLineList;

        D3D11_BUFFER_DESC vbDesc{};
        vbDesc.ByteWidth = static_cast<UINT>(vertices.size_bytes());
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbInit{ vertices.data(), 0, 0 };
        if (FAILED(m_device->CreateBuffer(&vbDesc, &vbInit, &buffers.vb)))
        {
            return nullptr;
        }

        D3D11_BUFFER_DESC ibDesc{};
        ibDesc.ByteWidth = static_cast<UINT>(indices.size_bytes());
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibInit{ indices.data(), 0, 0 };
        if (FAILED(m_device->CreateBuffer(&ibDesc, &ibInit, &buffers.ib)))
        {
            return nullptr;
        }

        const std::string keyStr(key);
        auto [it, inserted] = m_meshCache.insert_or_assign(keyStr, std::move(buffers));
        return &it->second;
    }

    const DX11MeshBuffers* DX11Device::UploadCpuMeshVertices(
        std::string_view key,
        std::span<const MeshVertex> vertices,
        std::span<const u32> indices,
        bool isLineList)
    {
        if (vertices.empty())
        {
            return nullptr;
        }
        const auto dx11Verts = ConvertMeshVertices(vertices);
        return UploadMeshData(key, dx11Verts, indices, isLineList);
    }

    std::expected<void, std::string> DX11Device::InitProceduralMeshes()
    {
        const char* primitiveNames[] = { "Cube", "Sphere", "Plane", "Grid", "Quad" };
        for (const char* name : primitiveNames)
        {
            std::vector<MeshVertex> verts;
            std::vector<u32> indices;
            Detail::BuildPrimitiveVertices(name, verts, indices);
            const bool isLine = (std::string_view(name) == "Grid");
            if (!UploadCpuMeshVertices(name, verts, indices, isLine))
            {
                return std::unexpected(std::format("Failed to upload DX11 procedural mesh '{}'", name));
            }
        }
        return {};
    }

    const DX11MeshBuffers* DX11Device::GetOrCreateMesh(const MeshComponent& mc)
    {
        if (mc.MeshRef && !mc.MeshRef->GetCpuVertices().empty())
        {
            std::string cacheKey;
            if (!mc.ModelPath.empty())
            {
                cacheKey = mc.ModelPath;
            }
            else if (!mc.MeshRef->GetName().empty() && mc.MeshRef->GetName() != "Mesh")
            {
                cacheKey = std::string(mc.MeshRef->GetName());
            }
            else if (!mc.MeshName.empty())
            {
                const std::string canonical = CanonicalPrimitiveName(mc.MeshName);
                if (canonical != "Cube" && canonical != "Sphere" && canonical != "Plane" && canonical != "Grid" && canonical != "Quad")
                {
                    cacheKey = mc.MeshName;
                }
                else
                {
                    cacheKey = std::format("MeshPtr_{:x}", reinterpret_cast<uintptr_t>(mc.MeshRef.get()));
                }
            }
            else
            {
                cacheKey = std::format("MeshPtr_{:x}", reinterpret_cast<uintptr_t>(mc.MeshRef.get()));
            }

            if (const auto it = m_meshCache.find(cacheKey); it != m_meshCache.end())
            {
                return &it->second;
            }

            const bool isLine = (CanonicalPrimitiveName(mc.MeshName) == "Grid" || mc.MeshRef->GetName() == "Grid");
            if (const auto* uploaded = UploadCpuMeshVertices(
                    cacheKey,
                    mc.MeshRef->GetCpuVertices(),
                    mc.MeshRef->GetCpuIndices(),
                    isLine))
            {
                return uploaded;
            }
        }

        if (!mc.ModelPath.empty())
        {
            if (const auto it = m_meshCache.find(mc.ModelPath); it != m_meshCache.end())
            {
                return &it->second;
            }

            std::filesystem::path resolvedPath = mc.ModelPath;
            if (const auto vfsResolved = VFS::Resolve(mc.ModelPath); vfsResolved && std::filesystem::exists(*vfsResolved))
            {
                resolvedPath = *vfsResolved;
            }
            else if (const auto assetsResolved = VFS::Resolve("assets://" + mc.ModelPath); assetsResolved && std::filesystem::exists(*assetsResolved))
            {
                resolvedPath = *assetsResolved;
            }

            MeshAsset loadedMesh;
            if (MeshAssetImporter::LoadCpuMesh(resolvedPath, loadedMesh) && !loadedMesh.vertices.empty())
            {
                if (const auto* uploaded = UploadCpuMeshVertices(mc.ModelPath, loadedMesh.vertices, loadedMesh.indices, false))
                {
                    return uploaded;
                }
            }
        }

        const std::string canonicalName = CanonicalPrimitiveName(mc.MeshName);
        if (const auto it = m_meshCache.find(canonicalName); it != m_meshCache.end())
        {
            return &it->second;
        }

        if (const auto it = m_meshCache.find("Cube"); it != m_meshCache.end())
        {
            return &it->second;
        }

        return nullptr;
    }

    std::expected<void, std::string> DX11Device::InitBasicShaders()
    {
        const char* shaderSource = R"(
            cbuffer CBuf : register(b0)
            {
                row_major float4x4 WorldViewProj;
                row_major float4x4 World;
                row_major float4x4 LightViewProj;
                float4 ObjectColor;
                float4 LightDir;
                float4 LightColor;
                float4 AmbientColor;
                float4 CameraPos;
                float4 FogParams;
                float4 PointLightPos;
                float4 PointLightColor;
                float4 SpotLightPos;
                float4 SpotLightDir;
                float4 SpotLightColor;
                float4 MaterialParams;
            };

            Texture2D diffuseTexture : register(t0);
            SamplerState linearSampler : register(s0);
            Texture2D shadowMap : register(t1);
            SamplerComparisonState shadowSampler : register(s1);
            Texture2D normalMap : register(t2);

            struct VSIn
            {
                float3 Pos : POSITION;
                float3 Normal : NORMAL;
                float2 UV : TEXCOORD0;
                float4 Color : COLOR;
                float4 Tangent : TANGENT;
            };

            struct PSIn
            {
                float4 Pos : SV_Position;
                float4 Color : COLOR;
                float3 Normal : NORMAL;
                float4 Tangent : TANGENT;
                float2 UV : TEXCOORD0;
                float4 LightSpacePos : TEXCOORD1;
                float3 WorldPos : TEXCOORD2;
            };

            PSIn VSMain(VSIn input)
            {
                PSIn output;
                float4 worldPos = mul(float4(input.Pos, 1.0f), World);
                output.Pos = mul(float4(input.Pos, 1.0f), WorldViewProj);
                output.Color = input.Color * ObjectColor;
                output.Normal = mul(input.Normal, (float3x3)World);
                output.Tangent = float4(mul(input.Tangent.xyz, (float3x3)World), input.Tangent.w);
                output.UV = input.UV;
                output.LightSpacePos = mul(worldPos, LightViewProj);
                output.WorldPos = worldPos.xyz;
                return output;
            }

            float4 ShadowVSMain(VSIn input) : SV_Position
            {
                float4 worldPos = mul(float4(input.Pos, 1.0f), World);
                return mul(worldPos, LightViewProj);
            }

            float CalculateShadow(float4 lightSpacePos)
            {
                float3 projCoords = lightSpacePos.xyz / max(lightSpacePos.w, 0.0001f);
                projCoords.x = projCoords.x * 0.5f + 0.5f;
                projCoords.y = -projCoords.y * 0.5f + 0.5f;

                if (projCoords.x < 0.0f || projCoords.x > 1.0f ||
                    projCoords.y < 0.0f || projCoords.y > 1.0f ||
                    projCoords.z < 0.0f || projCoords.z > 1.0f)
                {
                    return 1.0f;
                }

                float bias = 0.0015f;
                float shadow = 0.0f;
                float texelSize = 1.0f / 2048.0f;
                [unroll]
                for (int x = -1; x <= 1; ++x)
                {
                    [unroll]
                    for (int y = -1; y <= 1; ++y)
                    {
                        shadow += shadowMap.SampleCmpLevelZero(shadowSampler, projCoords.xy + float2(x, y) * texelSize, projCoords.z - bias);
                    }
                }
                return shadow / 9.0f;
            }

            float4 PSMain(PSIn input) : SV_Target
            {
                float4 texColor = diffuseTexture.Sample(linearSampler, input.UV);

                if (MaterialParams.w > 0.5f)
                {
                    float4 unlitColor = texColor * input.Color;
                    if (FogParams.w > 0.0f)
                    {
                        float viewDist = length(CameraPos.xyz - input.WorldPos);
                        float fogFactor = saturate(1.0f - exp(-FogParams.w * viewDist));
                        unlitColor.rgb = lerp(unlitColor.rgb, FogParams.rgb, fogFactor);
                    }
                    return unlitColor;
                }

                float nLenSq = dot(input.Normal, input.Normal);
                float3 N = (nLenSq > 1e-6f) ? (input.Normal * rsqrt(nLenSq)) : float3(0.0f, 1.0f, 0.0f);

                float3 T = input.Tangent.xyz - dot(input.Tangent.xyz, N) * N;
                float tLenSq = dot(T, T);
                if (tLenSq > 1e-5f)
                {
                    T = T * rsqrt(tLenSq);
                    float3 B = cross(N, T) * (input.Tangent.w == 0.0f ? 1.0f : input.Tangent.w);
                    float3x3 TBN = float3x3(T, B, N);
                    float3 normalSample = normalMap.Sample(linearSampler, input.UV).xyz * 2.0f - 1.0f;
                    float3 perturbedN = mul(normalSample, TBN);
                    float pnLenSq = dot(perturbedN, perturbedN);
                    if (pnLenSq > 1e-6f)
                    {
                        N = perturbedN * rsqrt(pnLenSq);
                    }
                }

                float3 L = normalize(-LightDir.xyz);
                float NdotL = max(dot(N, L), 0.0f);
                float shadowFactor = CalculateShadow(input.LightSpacePos);
                float ao = saturate(MaterialParams.z);
                float3 lit = AmbientColor.rgb * ao + LightColor.rgb * NdotL * shadowFactor;

                if (PointLightPos.w > 0.0f)
                {
                    float3 toPoint = PointLightPos.xyz - input.WorldPos;
                    float dist = length(toPoint);
                    if (dist < PointLightPos.w)
                    {
                        float3 L_pt = toPoint / max(dist, 0.0001f);
                        float NdotL_pt = max(dot(N, L_pt), 0.0f);
                        float att = pow(saturate(1.0f - (dist / PointLightPos.w)), 2.0f) / (1.0f + 0.1f * dist * dist);
                        lit += PointLightColor.rgb * NdotL_pt * att;
                    }
                }

                if (SpotLightPos.w > 0.0f)
                {
                    float3 toSpot = SpotLightPos.xyz - input.WorldPos;
                    float sDist = length(toSpot);
                    if (sDist < SpotLightPos.w)
                    {
                        float3 L_spot = toSpot / max(sDist, 0.0001f);
                        float theta = dot(-L_spot, normalize(SpotLightDir.xyz));
                        float outerCos = SpotLightDir.w;
                        float innerCos = SpotLightColor.w;
                        float spotFactor = saturate((theta - outerCos) / max(innerCos - outerCos, 0.001f));
                        float sAtt = pow(saturate(1.0f - (sDist / SpotLightPos.w)), 2.0f) / (1.0f + 0.1f * sDist * sDist);
                        float NdotL_spot = max(dot(N, L_spot), 0.0f);
                        lit += SpotLightColor.rgb * NdotL_spot * sAtt * spotFactor;
                    }
                }

                float3 finalRgb = lit * input.Color.rgb * texColor.rgb;
                if (FogParams.w > 0.0f)
                {
                    float viewDist = length(CameraPos.xyz - input.WorldPos);
                    float fogFactor = saturate(1.0f - exp(-FogParams.w * viewDist));
                    finalRgb = lerp(finalRgb, FogParams.rgb, fogFactor);
                }
                return float4(finalRgb, input.Color.a * texColor.a);
            }

            struct GridVSIn
            {
                float3 Pos : POSITION;
                float3 Normal : NORMAL;
                float2 UV : TEXCOORD;
                float4 Color : COLOR;
            };

            struct GridPSIn
            {
                float4 Pos : SV_Position;
                float4 Color : COLOR;
            };

            GridPSIn GridVSMain(GridVSIn input)
            {
                GridPSIn output;
                output.Pos = mul(float4(input.Pos, 1.0f), WorldViewProj);
                output.Color = input.Color;
                return output;
            }

            float4 GridPSMain(GridPSIn input) : SV_Target
            {
                return input.Color;
            }
        )";

        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> shadowVsBlob;
        ComPtr<ID3DBlob> errorBlob;

        HRESULT hr = D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
            {
                RL_LOG_ERROR("DX11 VS Compile failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            return std::unexpected("DX11 VS Compile failed");
        }

        hr = D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
            {
                RL_LOG_ERROR("DX11 PS Compile failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            return std::unexpected("DX11 PS Compile failed");
        }

        hr = D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "ShadowVSMain", "vs_5_0", 0, 0, &shadowVsBlob, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
            {
                RL_LOG_ERROR("DX11 Shadow VS Compile failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            return std::unexpected("DX11 Shadow VS Compile failed");
        }

        hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs);
        if (FAILED(hr)) return std::unexpected("CreateVertexShader failed");

        hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ps);
        if (FAILED(hr)) return std::unexpected("CreatePixelShader failed");

        hr = m_device->CreateVertexShader(shadowVsBlob->GetBufferPointer(), shadowVsBlob->GetBufferSize(), nullptr, &m_shadowVS);
        if (FAILED(hr)) return std::unexpected("CreateVertexShader for shadow failed");

        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        hr = m_device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);
        if (FAILED(hr)) return std::unexpected("CreateInputLayout failed");

        ComPtr<ID3DBlob> gridVsBlob;
        ComPtr<ID3DBlob> gridPsBlob;

        hr = D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "GridVSMain", "vs_5_0", 0, 0, &gridVsBlob, &errorBlob);
        if (FAILED(hr))
        {
            return std::unexpected("DX11 Grid VS Compile failed");
        }

        hr = D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "GridPSMain", "ps_5_0", 0, 0, &gridPsBlob, &errorBlob);
        if (FAILED(hr))
        {
            return std::unexpected("DX11 Grid PS Compile failed");
        }

        hr = m_device->CreateVertexShader(gridVsBlob->GetBufferPointer(), gridVsBlob->GetBufferSize(), nullptr, &m_gridVS);
        if (FAILED(hr)) return std::unexpected("CreateVertexShader for grid failed");

        hr = m_device->CreatePixelShader(gridPsBlob->GetBufferPointer(), gridPsBlob->GetBufferSize(), nullptr, &m_gridPS);
        if (FAILED(hr)) return std::unexpected("CreatePixelShader for grid failed");

        const D3D11_INPUT_ELEMENT_DESC gridLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        hr = m_device->CreateInputLayout(gridLayout, static_cast<UINT>(std::size(gridLayout)), gridVsBlob->GetBufferPointer(), gridVsBlob->GetBufferSize(), &m_gridInputLayout);
        if (FAILED(hr)) return std::unexpected("CreateInputLayout for grid failed");

        D3D11_BUFFER_DESC cbDesc{};
        cbDesc.ByteWidth = sizeof(CBLayout);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
        if (FAILED(hr)) return std::unexpected("CreateBuffer constant buffer failed");

        const f32 h = 0.5f;
        const DX11Vertex cubeVertices[24] = {
            { { -h, -h, -h }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },
            { { -h,  h, -h }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },
            { {  h,  h, -h }, {  0.0f,  0.0f, -1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },
            { {  h, -h, -h }, {  0.0f,  0.0f, -1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },

            { {  h, -h,  h }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { -1.0f,  0.0f,  0.0f, 1.0f } },
            { {  h,  h,  h }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { -1.0f,  0.0f,  0.0f, 1.0f } },
            { { -h,  h,  h }, {  0.0f,  0.0f,  1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { -1.0f,  0.0f,  0.0f, 1.0f } },
            { { -h, -h,  h }, {  0.0f,  0.0f,  1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { -1.0f,  0.0f,  0.0f, 1.0f } },

            { { -h,  h, -h }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },
            { { -h,  h,  h }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },
            { {  h,  h,  h }, {  0.0f,  1.0f,  0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },
            { {  h,  h, -h }, {  0.0f,  1.0f,  0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },

            { { -h, -h,  h }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },
            { { -h, -h, -h }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },
            { {  h, -h, -h }, {  0.0f, -1.0f,  0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },
            { {  h, -h,  h }, {  0.0f, -1.0f,  0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  1.0f,  0.0f,  0.0f, 1.0f } },

            { { -h, -h,  h }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  0.0f,  0.0f, -1.0f, 1.0f } },
            { { -h,  h,  h }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  0.0f,  0.0f, -1.0f, 1.0f } },
            { { -h,  h, -h }, { -1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  0.0f,  0.0f, -1.0f, 1.0f } },
            { { -h, -h, -h }, { -1.0f,  0.0f,  0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  0.0f,  0.0f, -1.0f, 1.0f } },

            { {  h, -h, -h }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  0.0f,  0.0f,  1.0f, 1.0f } },
            { {  h,  h, -h }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  0.0f,  0.0f,  1.0f, 1.0f } },
            { {  h,  h,  h }, {  1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  0.0f,  0.0f,  1.0f, 1.0f } },
            { {  h, -h,  h }, {  1.0f,  0.0f,  0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, {  0.0f,  0.0f,  1.0f, 1.0f } }
        };

        const u32 cubeIndices[36] = {
             0,  1,  2,  0,  2,  3,
             4,  5,  6,  4,  6,  7,
             8,  9, 10,  8, 10, 11,
            12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19,
            20, 21, 22, 20, 22, 23
        };

        D3D11_BUFFER_DESC vbDesc{};
        vbDesc.ByteWidth = sizeof(cubeVertices);
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbInit{ cubeVertices, 0, 0 };
        hr = m_device->CreateBuffer(&vbDesc, &vbInit, &m_cubeVertexBuffer);
        if (FAILED(hr)) return std::unexpected("CreateBuffer cube vertex buffer failed");

        D3D11_BUFFER_DESC ibDesc{};
        ibDesc.ByteWidth = sizeof(cubeIndices);
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibInit{ cubeIndices, 0, 0 };
        hr = m_device->CreateBuffer(&ibDesc, &ibInit, &m_cubeIndexBuffer);
        if (FAILED(hr)) return std::unexpected("CreateBuffer cube index buffer failed");

        struct Color4 { float r, g, b, a; };
        std::vector<DX11Vertex> gridVertices;
        const f32 halfSize = 20.0f;
        const u32 lines = 40;
        const f32 step = (halfSize * 2.0f) / static_cast<f32>(lines);
        gridVertices.reserve((lines + 1) * 4);

        for (u32 i = 0; i <= lines; ++i)
        {
            const f32 pos = -halfSize + static_cast<f32>(i) * step;
            const bool isCenter = (std::abs(pos) < 0.001f);

            const Color4 colorZ = isCenter
                ? Color4{ 0.25f, 0.5f, 1.0f, 1.0f }
                : Color4{ 0.35f, 0.35f, 0.35f, 1.0f };

            gridVertices.push_back({ { pos, 0.0f, -halfSize }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }, { colorZ.r, colorZ.g, colorZ.b, colorZ.a }, { 1.0f, 0.0f, 0.0f, 1.0f } });
            gridVertices.push_back({ { pos, 0.0f,  halfSize }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }, { colorZ.r, colorZ.g, colorZ.b, colorZ.a }, { 1.0f, 0.0f, 0.0f, 1.0f } });

            const Color4 colorX = isCenter
                ? Color4{ 1.0f, 0.25f, 0.25f, 1.0f }
                : Color4{ 0.35f, 0.35f, 0.35f, 1.0f };

            gridVertices.push_back({ { -halfSize, 0.0f, pos }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }, { colorX.r, colorX.g, colorX.b, colorX.a }, { 1.0f, 0.0f, 0.0f, 1.0f } });
            gridVertices.push_back({ {  halfSize, 0.0f, pos }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f }, { colorX.r, colorX.g, colorX.b, colorX.a }, { 1.0f, 0.0f, 0.0f, 1.0f } });
        }

        m_gridVertexCount = static_cast<u32>(gridVertices.size());

        D3D11_BUFFER_DESC gvbDesc{};
        gvbDesc.ByteWidth = static_cast<UINT>(gridVertices.size() * sizeof(DX11Vertex));
        gvbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        gvbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA gvbInit{ gridVertices.data(), 0, 0 };
        hr = m_device->CreateBuffer(&gvbDesc, &gvbInit, &m_gridVertexBuffer);
        if (FAILED(hr)) return std::unexpected("CreateBuffer grid vertex buffer failed");

        return {};
    }

    void DX11Device::RenderScene(Scene& scene)
    {
        if (!m_context || !m_vs || !m_ps || !m_cubeVertexBuffer || !m_constantBuffer) return;

        const WorldEnvironment& env = scene.GetEnvironment();
        const bool sceneWireframe = (scene.GetShadingMode() == SceneShadingMode::Wireframe);

        DirectX::XMMATRIX viewProj = scene.GetCamera().GetViewProjectionMatrix();
        const DirectX::XMFLOAT3& camPos3 = scene.GetCamera().GetPosition();
        const DirectX::XMFLOAT4 cameraPos{ camPos3.x, camPos3.y, camPos3.z, 1.0f };

        const auto* transformPool = scene.GetPool<TransformComponent>();
        const auto* meshPool = scene.GetPool<MeshComponent>();
        const auto* quadPool = scene.GetPool<QuadRenderComponent>();
        if (!transformPool) return;

        DirectX::XMFLOAT4 lightDir{ 0.5f, -0.8f, 0.7f, 0.0f };
        DirectX::XMFLOAT4 lightColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        const DirectX::XMFLOAT4 ambientColor{
            0.15f * env.AmbientStrength,
            0.18f * env.AmbientStrength,
            0.22f * env.AmbientStrength,
            env.AmbientStrength
        };
        const DirectX::XMFLOAT4 fogParams{
            env.FogColor.x,
            env.FogColor.y,
            env.FogColor.z,
            env.FogEnabled ? env.FogDensity : 0.0f
        };

        DirectX::XMFLOAT4 pointLightPos{ 0.0f, 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 pointLightColor{ 0.0f, 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 spotLightPos{ 0.0f, 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 spotLightDir{ 0.0f, -1.0f, 0.0f, 0.9f };
        DirectX::XMFLOAT4 spotLightColor{ 0.0f, 0.0f, 0.0f, 0.95f };

        bool foundDir = false;
        bool foundPoint = false;
        bool foundSpot = false;

        const auto* lightPool = scene.GetPool<LightComponent>();
        if (lightPool)
        {
            const auto lightEntities = lightPool->GetEntities();
            const auto lights = lightPool->GetData();
            for (usize i = 0; i < lights.size(); ++i)
            {
                const auto* tr = transformPool->TryGet(lightEntities[i]);
                if (lights[i].Type == LightType::Directional && !foundDir)
                {
                    if (tr && (tr->Position.x != 0.0f || tr->Position.y != 0.0f || tr->Position.z != 0.0f))
                    {
                        lightDir = { tr->Position.x, tr->Position.y, tr->Position.z, 0.0f };
                    }
                    lightColor = {
                        lights[i].Color.x * lights[i].Intensity,
                        lights[i].Color.y * lights[i].Intensity,
                        lights[i].Color.z * lights[i].Intensity,
                        1.0f
                    };
                    foundDir = true;
                }
                else if (lights[i].Type == LightType::Point && !foundPoint && tr)
                {
                    const f32 range = (lights[i].Range > 0.0f) ? lights[i].Range : 10.0f;
                    pointLightPos = { tr->Position.x, tr->Position.y, tr->Position.z, range };
                    pointLightColor = {
                        lights[i].Color.x * lights[i].Intensity,
                        lights[i].Color.y * lights[i].Intensity,
                        lights[i].Color.z * lights[i].Intensity,
                        lights[i].Intensity
                    };
                    foundPoint = true;
                }
                else if (lights[i].Type == LightType::Spot && !foundSpot && tr)
                {
                    const f32 range = (lights[i].Range > 0.0f) ? lights[i].Range : 15.0f;
                    const f32 clampedAngle = std::clamp(lights[i].SpotAngle, 1.0f, 179.0f);
                    const f32 outerCos = std::cos(DirectX::XMConvertToRadians(clampedAngle * 0.5f));
                    const f32 innerCos = std::cos(DirectX::XMConvertToRadians(clampedAngle * 0.35f));

                    const auto rotMat = DirectX::XMMatrixRotationRollPitchYaw(tr->Rotation.x, tr->Rotation.y, tr->Rotation.z);
                    DirectX::XMFLOAT3 dir3;
                    DirectX::XMStoreFloat3(&dir3, DirectX::XMVector3Normalize(
                        DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotMat)));

                    spotLightPos = { tr->Position.x, tr->Position.y, tr->Position.z, range };
                    spotLightDir = { dir3.x, dir3.y, dir3.z, outerCos };
                    spotLightColor = {
                        lights[i].Color.x * lights[i].Intensity,
                        lights[i].Color.y * lights[i].Intensity,
                        lights[i].Color.z * lights[i].Intensity,
                        innerCos
                    };
                    foundSpot = true;
                }
            }
        }

        DirectX::XMVECTOR lightDirVec = DirectX::XMVector3Normalize(DirectX::XMVectorSet(lightDir.x, lightDir.y, lightDir.z, 0.0f));
        DirectX::XMVECTOR lightEye = DirectX::XMVectorScale(lightDirVec, -40.0f);
        DirectX::XMVECTOR lightAt = DirectX::XMVectorZero();
        DirectX::XMVECTOR lightUp = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        if (std::abs(DirectX::XMVectorGetY(lightDirVec)) > 0.99f)
        {
            lightUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        }
        DirectX::XMMATRIX lightView = DirectX::XMMatrixLookAtLH(lightEye, lightAt, lightUp);
        DirectX::XMMATRIX lightProj = DirectX::XMMatrixOrthographicLH(40.0f, 40.0f, 1.0f, 100.0f);
        DirectX::XMMATRIX lightViewProj = DirectX::XMMatrixMultiply(lightView, lightProj);

        auto writeCommonCB = [&](CBLayout* cb,
                                 const DirectX::XMMATRIX& wvp,
                                 const DirectX::XMMATRIX& world,
                                 const DirectX::XMFLOAT4& objectColor,
                                 const DirectX::XMFLOAT4& materialParams) {
            DirectX::XMStoreFloat4x4(&cb->WorldViewProj, wvp);
            DirectX::XMStoreFloat4x4(&cb->World, world);
            DirectX::XMStoreFloat4x4(&cb->LightViewProj, lightViewProj);
            cb->ObjectColor = objectColor;
            cb->LightDir = lightDir;
            cb->LightColor = lightColor;
            cb->AmbientColor = ambientColor;
            cb->CameraPos = cameraPos;
            cb->FogParams = fogParams;
            cb->PointLightPos = pointLightPos;
            cb->PointLightColor = pointLightColor;
            cb->SpotLightPos = spotLightPos;
            cb->SpotLightDir = spotLightDir;
            cb->SpotLightColor = spotLightColor;
            cb->MaterialParams = materialParams;
        };

        if (m_shadowDSV && m_shadowVS && meshPool)
        {
            ID3D11ShaderResourceView* nullSRV = nullptr;
            m_context->PSSetShaderResources(1, 1, &nullSRV);

            m_context->OMSetRenderTargets(0, nullptr, m_shadowDSV.Get());
            m_context->ClearDepthStencilView(m_shadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

            if (m_depthState)
            {
                m_context->OMSetDepthStencilState(m_depthState.Get(), 1);
            }
            if (m_rasterState)
            {
                m_context->RSSetState(m_rasterState.Get());
            }

            D3D11_VIEWPORT shadowVp{};
            shadowVp.Width = 2048.0f;
            shadowVp.Height = 2048.0f;
            shadowVp.MinDepth = 0.0f;
            shadowVp.MaxDepth = 1.0f;
            shadowVp.TopLeftX = 0.0f;
            shadowVp.TopLeftY = 0.0f;
            m_context->RSSetViewports(1, &shadowVp);

            m_context->IASetInputLayout(m_inputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            m_context->VSSetShader(m_shadowVS.Get(), nullptr, 0);
            m_context->PSSetShader(nullptr, nullptr, 0);

            ID3D11Buffer* cbs[] = { m_constantBuffer.Get() };
            m_context->VSSetConstantBuffers(0, 1, cbs);

            const auto shadowEntities = meshPool->GetEntities();
            const auto shadowMeshes = meshPool->GetData();
            const UINT stride = sizeof(DX11Vertex);
            const UINT offset = 0;

            for (usize i = 0; i < shadowMeshes.size(); ++i)
            {
                if (!shadowMeshes[i].IsVisible) continue;
                Entity entity = scene.GetEntityFromIndex(shadowEntities[i]);
                if (!scene.IsValid(entity)) continue;

                const DX11MeshBuffers* meshBuf = GetOrCreateMesh(shadowMeshes[i]);
                if (!meshBuf || !meshBuf->vb || !meshBuf->ib || meshBuf->indexCount == 0 || meshBuf->isLineList)
                {
                    continue;
                }

                ID3D11Buffer* vbs[] = { meshBuf->vb.Get() };
                m_context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
                m_context->IASetIndexBuffer(meshBuf->ib.Get(), DXGI_FORMAT_R32_UINT, 0);

                DirectX::XMMATRIX world = scene.GetWorldMatrix(entity);

                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (SUCCEEDED(m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                {
                    writeCommonCB(
                        static_cast<CBLayout*>(mapped.pData),
                        DirectX::XMMatrixMultiply(world, lightViewProj),
                        world,
                        shadowMeshes[i].Color,
                        { 0.0f, 0.5f, 1.0f, 0.0f }
                    );
                    m_context->Unmap(m_constantBuffer.Get(), 0);
                }

                m_context->DrawIndexed(meshBuf->indexCount, 0, 0);
            }
        }

        ID3D11RenderTargetView* rtvs[] = { m_rtv.Get() };
        m_context->OMSetRenderTargets(1, rtvs, m_dsv.Get());
        ClearRenderTarget(env.SkyColor);
        if (m_depthState)
        {
            m_context->OMSetDepthStencilState(m_depthState.Get(), 1);
        }
        if (m_rasterState)
        {
            m_context->RSSetState(m_rasterState.Get());
        }

        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<f32>(m_width);
        vp.Height = static_cast<f32>(m_height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        m_context->RSSetViewports(1, &vp);

        if (m_showGrid && m_gridVertexBuffer && m_gridVertexCount > 0 && m_gridVS && m_gridPS && m_gridInputLayout)
        {
            DirectX::XMMATRIX identity = DirectX::XMMatrixIdentity();
            DirectX::XMMATRIX gridWvp = identity * viewProj;

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                writeCommonCB(
                    static_cast<CBLayout*>(mapped.pData),
                    gridWvp,
                    identity,
                    DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f },
                    DirectX::XMFLOAT4{ 0.0f, 0.5f, 1.0f, 0.0f }
                );
                m_context->Unmap(m_constantBuffer.Get(), 0);
            }

            m_context->IASetInputLayout(m_gridInputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

            const UINT gridStride = sizeof(DX11Vertex);
            const UINT gridOffset = 0;
            ID3D11Buffer* gridVbs[] = { m_gridVertexBuffer.Get() };
            m_context->IASetVertexBuffers(0, 1, gridVbs, &gridStride, &gridOffset);
            m_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

            m_context->VSSetShader(m_gridVS.Get(), nullptr, 0);
            m_context->PSSetShader(m_gridPS.Get(), nullptr, 0);

            ID3D11Buffer* cbs[] = { m_constantBuffer.Get() };
            m_context->VSSetConstantBuffers(0, 1, cbs);
            m_context->PSSetConstantBuffers(0, 1, cbs);

            m_context->Draw(m_gridVertexCount, 0);
        }

        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->VSSetShader(m_vs.Get(), nullptr, 0);
        m_context->PSSetShader(m_ps.Get(), nullptr, 0);

        ID3D11Buffer* cbs[] = { m_constantBuffer.Get() };
        m_context->VSSetConstantBuffers(0, 1, cbs);
        m_context->PSSetConstantBuffers(0, 1, cbs);

        ID3D11SamplerState* samplers[2] = { m_linearSampler.Get(), m_shadowSampler.Get() };
        m_context->PSSetSamplers(0, 2, samplers);

        if (m_shadowSRV)
        {
            ID3D11ShaderResourceView* shadowSrvs[] = { m_shadowSRV.Get() };
            m_context->PSSetShaderResources(1, 1, shadowSrvs);
        }

        const UINT stride = sizeof(DX11Vertex);
        const UINT offset = 0;

        if (meshPool)
        {
            const auto* materialPool = scene.GetPool<MaterialComponent>();
            const auto entities = meshPool->GetEntities();
            const auto meshes = meshPool->GetData();

            for (usize i = 0; i < meshes.size(); ++i)
            {
                if (!meshes[i].IsVisible) continue;
                Entity entity = scene.GetEntityFromIndex(entities[i]);
                if (!scene.IsValid(entity)) continue;

                const DX11MeshBuffers* meshBuf = GetOrCreateMesh(meshes[i]);
                if (!meshBuf || !meshBuf->vb || !meshBuf->ib || meshBuf->indexCount == 0)
                {
                    continue;
                }

                const bool useWireframe = sceneWireframe || meshes[i].Wireframe;
                if (useWireframe && m_wireframeRasterState)
                {
                    m_context->RSSetState(m_wireframeRasterState.Get());
                }
                else if (m_rasterState)
                {
                    m_context->RSSetState(m_rasterState.Get());
                }

                m_context->IASetPrimitiveTopology(
                    meshBuf->isLineList ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                ID3D11Buffer* vbs[] = { meshBuf->vb.Get() };
                m_context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
                m_context->IASetIndexBuffer(meshBuf->ib.Get(), DXGI_FORMAT_R32_UINT, 0);

                std::string texPath = meshes[i].TexturePath;
                std::string normPath;
                DirectX::XMFLOAT4 objColor = meshes[i].Color;
                DirectX::XMFLOAT4 matParams{ 0.0f, 0.5f, 1.0f, 0.0f };

                if (materialPool)
                {
                    const auto* mat = materialPool->TryGet(entities[i]);
                    if (mat)
                    {
                        objColor.x *= mat->BaseColor.x * mat->AlbedoColor.x;
                        objColor.y *= mat->BaseColor.y * mat->AlbedoColor.y;
                        objColor.z *= mat->BaseColor.z * mat->AlbedoColor.z;
                        objColor.w *= mat->BaseColor.w * mat->AlbedoColor.w;
                        matParams = { mat->Metallic, mat->Roughness, mat->AO, 0.0f };
                        if (texPath.empty() && !mat->TexturePath.empty())
                        {
                            texPath = mat->TexturePath;
                        }
                        if (!mat->NormalMapPath.empty())
                        {
                            normPath = mat->NormalMapPath;
                        }
                    }
                }

                ID3D11ShaderResourceView* texSrv = GetOrCreateTexture(texPath);
                ID3D11ShaderResourceView* normSrv = m_fallbackNormalSRV.Get();
                if (!normPath.empty())
                {
                    ID3D11ShaderResourceView* loadedNorm = GetOrCreateTexture(normPath);
                    if (loadedNorm && loadedNorm != m_fallbackSRV.Get())
                    {
                        normSrv = loadedNorm;
                    }
                }
                m_context->PSSetShaderResources(0, 1, &texSrv);
                m_context->PSSetShaderResources(2, 1, &normSrv);

                DirectX::XMMATRIX world = scene.GetWorldMatrix(entity);
                DirectX::XMMATRIX wvp = world * viewProj;

                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (SUCCEEDED(m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                {
                    writeCommonCB(static_cast<CBLayout*>(mapped.pData), wvp, world, objColor, matParams);
                    m_context->Unmap(m_constantBuffer.Get(), 0);
                }

                m_context->DrawIndexed(meshBuf->indexCount, 0, 0);
            }
        }

        if (quadPool)
        {
            const auto quadIt = m_meshCache.find("Quad");
            if (quadIt != m_meshCache.end() && quadIt->second.vb && quadIt->second.ib && quadIt->second.indexCount > 0)
            {
                const DX11MeshBuffers& quadBuf = quadIt->second;

                if (sceneWireframe && m_wireframeRasterState)
                {
                    m_context->RSSetState(m_wireframeRasterState.Get());
                }
                else if (m_rasterState)
                {
                    m_context->RSSetState(m_rasterState.Get());
                }

                if (m_alphaBlendState)
                {
                    const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    m_context->OMSetBlendState(m_alphaBlendState.Get(), blendFactor, 0xFFFFFFFF);
                }

                m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ID3D11Buffer* qVbs[] = { quadBuf.vb.Get() };
                m_context->IASetVertexBuffers(0, 1, qVbs, &stride, &offset);
                m_context->IASetIndexBuffer(quadBuf.ib.Get(), DXGI_FORMAT_R32_UINT, 0);

                ID3D11ShaderResourceView* normSrv = m_fallbackNormalSRV.Get();
                m_context->PSSetShaderResources(2, 1, &normSrv);

                const auto quadEntities = quadPool->GetEntities();
                const auto quads = quadPool->GetData();

                for (usize i = 0; i < quads.size(); ++i)
                {
                    if (!quads[i].IsVisible) continue;
                    Entity entity = scene.GetEntityFromIndex(quadEntities[i]);
                    if (!scene.IsValid(entity)) continue;

                    ID3D11ShaderResourceView* texSrv = GetOrCreateTexture(quads[i].TexturePath);
                    m_context->PSSetShaderResources(0, 1, &texSrv);

                    DirectX::XMMATRIX world = scene.GetWorldMatrix(entity);
                    DirectX::XMMATRIX wvp = world * viewProj;

                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    if (SUCCEEDED(m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                    {
                        writeCommonCB(
                            static_cast<CBLayout*>(mapped.pData),
                            wvp,
                            world,
                            quads[i].Color,
                            DirectX::XMFLOAT4{ 0.0f, 0.5f, 1.0f, 1.0f }
                        );
                        m_context->Unmap(m_constantBuffer.Get(), 0);
                    }

                    m_context->DrawIndexed(quadBuf.indexCount, 0, 0);
                }

                m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
            }
        }

        if (m_rasterState)
        {
            m_context->RSSetState(m_rasterState.Get());
        }

        ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
        m_context->PSSetShaderResources(0, 3, nullSRVs);
    }
}
