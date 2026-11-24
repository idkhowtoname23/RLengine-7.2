#pragma once

#include "Assets/AssetManager.hpp"
#include "Core/Logger.hpp"
#include "Graphics/DX12/Mesh.hpp"

#include <stb_image.h>
#include <cgltf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace RL
{
    struct MeshAsset final : public AssetBase
    {
        std::vector<MeshVertex> vertices;
        std::vector<u32> indices;
        DirectX::XMFLOAT3 boundsMin{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 boundsMax{ 0.0f, 0.0f, 0.0f };
        std::shared_ptr<Mesh> dx12Mesh;

        void ComputeBounds() noexcept
        {
            if (vertices.empty())
            {
                boundsMin = { 0.0f, 0.0f, 0.0f };
                boundsMax = { 0.0f, 0.0f, 0.0f };
                return;
            }

            DirectX::XMFLOAT3 bMin = vertices[0].Position;
            DirectX::XMFLOAT3 bMax = vertices[0].Position;
            for (const auto& v : vertices)
            {
                bMin.x = (std::min)(bMin.x, v.Position.x);
                bMin.y = (std::min)(bMin.y, v.Position.y);
                bMin.z = (std::min)(bMin.z, v.Position.z);
                bMax.x = (std::max)(bMax.x, v.Position.x);
                bMax.y = (std::max)(bMax.y, v.Position.y);
                bMax.z = (std::max)(bMax.z, v.Position.z);
            }
            boundsMin = bMin;
            boundsMax = bMax;
        }
    };

    struct TextureAsset final : public AssetBase
    {
        u32 width = 0;
        u32 height = 0;
        u32 channels = 0;
        std::vector<u8> pixels;
        std::string sourcePath;
        u32 textureIndex = 0;
    };

    struct ScriptAsset final : public AssetBase
    {
        std::string source;
    };

    namespace Detail
    {
        [[nodiscard]] inline std::string ToLowerExt(std::string_view ext)
        {
            std::string result(ext);
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return result;
        }

        inline void ComputeOrthogonalTangent(MeshVertex& v) noexcept
        {
            const DirectX::XMFLOAT3 n = v.Normal;
            const DirectX::XMFLOAT3 t = (std::abs(n.x) > 0.9f)
                ? DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f)
                : DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f);
            const DirectX::XMVECTOR vn = DirectX::XMLoadFloat3(&n);
            const DirectX::XMVECTOR vt = DirectX::XMLoadFloat3(&t);
            const DirectX::XMVECTOR dot = DirectX::XMVector3Dot(vn, vt);
            DirectX::XMVECTOR ortho = DirectX::XMVectorSubtract(vt, DirectX::XMVectorMultiply(vn, dot));
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(ortho)) > 1e-6f)
            {
                ortho = DirectX::XMVector3Normalize(ortho);
                DirectX::XMFLOAT3 normT;
                DirectX::XMStoreFloat3(&normT, ortho);
                v.Tangent = { normT.x, normT.y, normT.z, 1.0f };
            }
            else
            {
                v.Tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
            }
        }

        inline void BuildPrimitiveVertices(
            std::string_view name,
            std::vector<MeshVertex>& outVertices,
            std::vector<u32>& outIndices)
        {
            outVertices.clear();
            outIndices.clear();

            const std::string lower = ToLowerExt(name);
            if (lower == "sphere")
            {
                constexpr f32 radius = 0.5f;
                constexpr u32 slices = 48;
                constexpr u32 stacks = 32;
                const f32 pi = std::numbers::pi_v<f32>;

                outVertices.reserve((stacks + 1) * (slices + 1));
                outIndices.reserve(stacks * slices * 6);

                for (u32 stack = 0; stack <= stacks; ++stack)
                {
                    const f32 phi = static_cast<f32>(stack) * pi / static_cast<f32>(stacks);
                    const f32 sinPhi = std::sin(phi);
                    const f32 cosPhi = std::cos(phi);

                    for (u32 slice = 0; slice <= slices; ++slice)
                    {
                        const f32 theta = static_cast<f32>(slice) * 2.0f * pi / static_cast<f32>(slices);
                        const f32 sinTheta = std::sin(theta);
                        const f32 cosTheta = std::cos(theta);

                        MeshVertex v{};
                        v.Normal = { sinPhi * cosTheta, cosPhi, sinPhi * sinTheta };
                        v.Position = { v.Normal.x * radius, v.Normal.y * radius, v.Normal.z * radius };
                        v.UV = { static_cast<f32>(slice) / static_cast<f32>(slices), static_cast<f32>(stack) / static_cast<f32>(stacks) };
                        v.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
                        v.Tangent = { -sinTheta, 0.0f, cosTheta, 1.0f };
                        outVertices.push_back(v);
                    }
                }

                for (u32 stack = 0; stack < stacks; ++stack)
                {
                    for (u32 slice = 0; slice < slices; ++slice)
                    {
                        const u32 first = stack * (slices + 1) + slice;
                        const u32 second = first + slices + 1;

                        outIndices.push_back(first);
                        outIndices.push_back(second);
                        outIndices.push_back(first + 1);

                        outIndices.push_back(second);
                        outIndices.push_back(second + 1);
                        outIndices.push_back(first + 1);
                    }
                }
                return;
            }

            if (lower == "plane")
            {
                constexpr f32 width = 10.0f;
                constexpr f32 depth = 10.0f;
                constexpr u32 subdivisions = 10;
                const u32 countX = subdivisions + 1;
                const u32 countZ = subdivisions + 1;
                const f32 halfW = width * 0.5f;
                const f32 halfD = depth * 0.5f;
                const f32 dx = width / static_cast<f32>(subdivisions);
                const f32 dz = depth / static_cast<f32>(subdivisions);

                outVertices.reserve(countX * countZ);
                outIndices.reserve(subdivisions * subdivisions * 6);

                for (u32 z = 0; z < countZ; ++z)
                {
                    const f32 posZ = -halfD + static_cast<f32>(z) * dz;
                    const f32 vCoord = static_cast<f32>(z) / static_cast<f32>(subdivisions);
                    for (u32 x = 0; x < countX; ++x)
                    {
                        const f32 posX = -halfW + static_cast<f32>(x) * dx;
                        const f32 uCoord = static_cast<f32>(x) / static_cast<f32>(subdivisions);

                        MeshVertex v{};
                        v.Position = { posX, 0.0f, posZ };
                        v.Normal = { 0.0f, 1.0f, 0.0f };
                        v.UV = { uCoord, vCoord };
                        v.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
                        v.Tangent = { 1.0f, 0.0f, 0.0f, -1.0f };
                        outVertices.push_back(v);
                    }
                }

                for (u32 z = 0; z < subdivisions; ++z)
                {
                    for (u32 x = 0; x < subdivisions; ++x)
                    {
                        const u32 row1 = z * countX;
                        const u32 row2 = (z + 1) * countX;

                        outIndices.push_back(row1 + x);
                        outIndices.push_back(row2 + x);
                        outIndices.push_back(row1 + x + 1);

                        outIndices.push_back(row1 + x + 1);
                        outIndices.push_back(row2 + x);
                        outIndices.push_back(row2 + x + 1);
                    }
                }
                return;
            }

            if (lower == "grid")
            {
                constexpr f32 size = 200.0f;
                constexpr u32 lines = 200;
                const f32 halfSize = size * 0.5f;
                const f32 step = size / static_cast<f32>(lines);

                outVertices.reserve((lines + 1) * 4);
                outIndices.reserve((lines + 1) * 4);

                u32 idx = 0;
                for (u32 i = 0; i <= lines; ++i)
                {
                    const f32 pos = -halfSize + static_cast<f32>(i) * step;
                    const bool isCenter = (std::abs(pos) < 0.001f);
                    const i32 gridStepIdx = static_cast<i32>(std::round(pos / step));
                    const bool isMajor = (std::abs(gridStepIdx) % 10 == 0);

                    DirectX::XMFLOAT4 colorZ{ 0.25f, 0.26f, 0.28f, 0.45f };
                    if (isCenter) colorZ = { 0.2f, 0.5f, 1.0f, 1.0f };
                    else if (isMajor) colorZ = { 0.55f, 0.58f, 0.62f, 0.85f };

                    outVertices.push_back({ { pos, 0.0f, -halfSize }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }, colorZ, { 1.0f, 0.0f, 0.0f, 1.0f } });
                    outVertices.push_back({ { pos, 0.0f,  halfSize }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }, colorZ, { 1.0f, 0.0f, 0.0f, 1.0f } });
                    outIndices.push_back(idx++);
                    outIndices.push_back(idx++);

                    DirectX::XMFLOAT4 colorX{ 0.25f, 0.26f, 0.28f, 0.45f };
                    if (isCenter) colorX = { 1.0f, 0.25f, 0.25f, 1.0f };
                    else if (isMajor) colorX = { 0.55f, 0.58f, 0.62f, 0.85f };

                    outVertices.push_back({ { -halfSize, 0.0f, pos }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }, colorX, { 1.0f, 0.0f, 0.0f, 1.0f } });
                    outVertices.push_back({ {  halfSize, 0.0f, pos }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f }, colorX, { 1.0f, 0.0f, 0.0f, 1.0f } });
                    outIndices.push_back(idx++);
                    outIndices.push_back(idx++);
                }
                return;
            }

            if (lower == "quad")
            {
                outVertices = {
                    { { -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
                    { {  0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
                    { {  0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
                    { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } }
                };
                outIndices = { 0, 1, 2, 0, 2, 3 };
                return;
            }

            constexpr f32 h = 0.5f;
            outVertices = {
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
            outIndices = {
                 0,  1,  2,  0,  2,  3,
                 4,  5,  6,  4,  6,  7,
                 8,  9, 10,  8, 10, 11,
                12, 13, 14, 12, 14, 15,
                16, 17, 18, 16, 18, 19,
                20, 21, 22, 20, 22, 23
            };
        }
    }

    class MeshAssetImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] bool Handles(std::string_view extension) const override
        {
            const std::string ext = Detail::ToLowerExt(extension);
            return ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".rlmesh";
        }

        [[nodiscard]] static bool LoadCpuMesh(const std::filesystem::path& realPath, MeshAsset& outAsset)
        {
            if (!std::filesystem::exists(realPath))
            {
                return false;
            }

            const std::string ext = Detail::ToLowerExt(realPath.extension().string());
            if (ext == ".gltf" || ext == ".glb")
            {
                return LoadGLTF(realPath, outAsset);
            }
            if (ext == ".obj")
            {
                return LoadOBJ(realPath, outAsset);
            }
            if (ext == ".rlmesh")
            {
                return LoadRLMesh(realPath, outAsset);
            }
            return false;
        }

        std::shared_ptr<AssetBase> Import(const std::string&,
                                          const std::filesystem::path& realPath) override
        {
            auto asset = std::make_shared<MeshAsset>();
            if (!LoadCpuMesh(realPath, *asset))
            {
                return nullptr;
            }
            return asset;
        }

    private:
        static bool LoadGLTF(const std::filesystem::path& realPath, MeshAsset& outAsset)
        {
            const std::string pathStr = realPath.string();
            cgltf_options options{};
            cgltf_data* data = nullptr;

            if (cgltf_parse_file(&options, pathStr.c_str(), &data) != cgltf_result_success || !data)
            {
                return false;
            }

            if (cgltf_load_buffers(&options, data, pathStr.c_str()) != cgltf_result_success)
            {
                cgltf_free(data);
                return false;
            }

            outAsset.vertices.clear();
            outAsset.indices.clear();

            for (cgltf_size m = 0; m < data->meshes_count; ++m)
            {
                const cgltf_mesh& mesh = data->meshes[m];
                for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
                {
                    const cgltf_primitive& prim = mesh.primitives[p];
                    const cgltf_accessor* posAcc = nullptr;
                    const cgltf_accessor* normAcc = nullptr;
                    const cgltf_accessor* uvAcc = nullptr;
                    const cgltf_accessor* colAcc = nullptr;
                    const cgltf_accessor* tanAcc = nullptr;
                    const cgltf_accessor* jointAcc = nullptr;
                    const cgltf_accessor* weightAcc = nullptr;

                    for (cgltf_size a = 0; a < prim.attributes_count; ++a)
                    {
                        const cgltf_attribute& attr = prim.attributes[a];
                        switch (attr.type)
                        {
                            case cgltf_attribute_type_position: posAcc = attr.data; break;
                            case cgltf_attribute_type_normal:   normAcc = attr.data; break;
                            case cgltf_attribute_type_texcoord: if (!uvAcc) uvAcc = attr.data; break;
                            case cgltf_attribute_type_color:    if (!colAcc) colAcc = attr.data; break;
                            case cgltf_attribute_type_tangent:  tanAcc = attr.data; break;
                            case cgltf_attribute_type_joints:   if (!jointAcc) jointAcc = attr.data; break;
                            case cgltf_attribute_type_weights:  if (!weightAcc) weightAcc = attr.data; break;
                            default: break;
                        }
                    }

                    if (!posAcc || posAcc->count == 0)
                    {
                        continue;
                    }

                    const u32 baseVertex = static_cast<u32>(outAsset.vertices.size());
                    const cgltf_size vertCount = posAcc->count;
                    outAsset.vertices.resize(baseVertex + vertCount);

                    for (cgltf_size i = 0; i < vertCount; ++i)
                    {
                        MeshVertex& v = outAsset.vertices[baseVertex + i];
                        float pos[3]{ 0.0f, 0.0f, 0.0f };
                        cgltf_accessor_read_float(posAcc, i, pos, 3);
                        v.Position = { pos[0], pos[1], pos[2] };

                        if (normAcc)
                        {
                            float norm[3]{ 0.0f, 1.0f, 0.0f };
                            cgltf_accessor_read_float(normAcc, i, norm, 3);
                            v.Normal = { norm[0], norm[1], norm[2] };
                        }
                        if (uvAcc)
                        {
                            float uv[2]{ 0.0f, 0.0f };
                            cgltf_accessor_read_float(uvAcc, i, uv, 2);
                            v.UV = { uv[0], uv[1] };
                        }
                        if (colAcc)
                        {
                            float col[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
                            cgltf_accessor_read_float(colAcc, i, col, 4);
                            v.Color = { col[0], col[1], col[2], col[3] };
                        }
                        if (tanAcc)
                        {
                            float tan[4]{ 1.0f, 0.0f, 0.0f, 1.0f };
                            cgltf_accessor_read_float(tanAcc, i, tan, 4);
                            v.Tangent = { tan[0], tan[1], tan[2], tan[3] };
                        }
                        else
                        {
                            Detail::ComputeOrthogonalTangent(v);
                        }
                        if (jointAcc)
                        {
                            cgltf_uint j[4]{ 0, 0, 0, 0 };
                            cgltf_accessor_read_uint(jointAcc, i, j, 4);
                            v.BoneIndices = { static_cast<u32>(j[0]), static_cast<u32>(j[1]), static_cast<u32>(j[2]), static_cast<u32>(j[3]) };
                        }
                        if (weightAcc)
                        {
                            float w[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                            cgltf_accessor_read_float(weightAcc, i, w, 4);
                            v.BoneWeights = { w[0], w[1], w[2], w[3] };
                        }
                    }

                    if (prim.indices)
                    {
                        const cgltf_size idxCount = prim.indices->count;
                        outAsset.indices.reserve(outAsset.indices.size() + idxCount);
                        for (cgltf_size i = 0; i < idxCount; ++i)
                        {
                            const cgltf_size raw = cgltf_accessor_read_index(prim.indices, i);
                            const u32 localIdx = (raw < vertCount) ? static_cast<u32>(raw) : 0u;
                            outAsset.indices.push_back(baseVertex + localIdx);
                        }
                    }
                    else
                    {
                        outAsset.indices.reserve(outAsset.indices.size() + vertCount);
                        for (cgltf_size i = 0; i < vertCount; ++i)
                        {
                            outAsset.indices.push_back(baseVertex + static_cast<u32>(i));
                        }
                    }
                }
            }

            cgltf_free(data);
            if (outAsset.vertices.empty())
            {
                return false;
            }
            outAsset.ComputeBounds();
            return true;
        }

        static bool LoadOBJ(const std::filesystem::path& realPath, MeshAsset& outAsset)
        {
            std::ifstream file(realPath);
            if (!file.is_open())
            {
                return false;
            }

            std::vector<DirectX::XMFLOAT3> positions;
            std::vector<DirectX::XMFLOAT2> uvs;
            std::vector<DirectX::XMFLOAT3> normals;
            outAsset.vertices.clear();
            outAsset.indices.clear();

            auto resolveObjIndex = [](i64 idx, usize count) -> usize {
                if (idx > 0) return static_cast<usize>(idx - 1);
                if (idx < 0)
                {
                    const i64 resolved = static_cast<i64>(count) + idx;
                    return (resolved >= 0) ? static_cast<usize>(resolved) : 0;
                }
                return 0;
            };

            std::string line;
            while (std::getline(file, line))
            {
                if (line.empty() || line[0] == '#') continue;

                std::istringstream iss(line);
                std::string prefix;
                iss >> prefix;

                if (prefix == "v")
                {
                    DirectX::XMFLOAT3 p{ 0.0f, 0.0f, 0.0f };
                    iss >> p.x >> p.y >> p.z;
                    positions.push_back(p);
                }
                else if (prefix == "vt")
                {
                    DirectX::XMFLOAT2 uv{ 0.0f, 0.0f };
                    iss >> uv.x >> uv.y;
                    uv.y = 1.0f - uv.y;
                    uvs.push_back(uv);
                }
                else if (prefix == "vn")
                {
                    DirectX::XMFLOAT3 n{ 0.0f, 1.0f, 0.0f };
                    iss >> n.x >> n.y >> n.z;
                    normals.push_back(n);
                }
                else if (prefix == "f")
                {
                    std::vector<MeshVertex> faceVerts;
                    std::string token;
                    bool hasNormal = false;

                    while (iss >> token)
                    {
                        MeshVertex v{};
                        i64 vi = 0;
                        i64 ti = 0;
                        i64 ni = 0;

                        const usize s1 = token.find('/');
                        if (s1 == std::string::npos)
                        {
                            vi = std::stoll(token);
                        }
                        else
                        {
                            vi = std::stoll(token.substr(0, s1));
                            const usize s2 = token.find('/', s1 + 1);
                            if (s2 == std::string::npos)
                            {
                                if (s1 + 1 < token.size()) ti = std::stoll(token.substr(s1 + 1));
                            }
                            else
                            {
                                if (s2 > s1 + 1) ti = std::stoll(token.substr(s1 + 1, s2 - (s1 + 1)));
                                if (s2 + 1 < token.size()) ni = std::stoll(token.substr(s2 + 1));
                            }
                        }

                        if (!positions.empty())
                        {
                            const usize pIdx = resolveObjIndex(vi, positions.size());
                            if (pIdx < positions.size()) v.Position = positions[pIdx];
                        }
                        if (ti != 0 && !uvs.empty())
                        {
                            const usize tIdx = resolveObjIndex(ti, uvs.size());
                            if (tIdx < uvs.size()) v.UV = uvs[tIdx];
                        }
                        if (ni != 0 && !normals.empty())
                        {
                            const usize nIdx = resolveObjIndex(ni, normals.size());
                            if (nIdx < normals.size())
                            {
                                v.Normal = normals[nIdx];
                                hasNormal = true;
                            }
                        }
                        faceVerts.push_back(v);
                    }

                    if (faceVerts.size() >= 3)
                    {
                        if (!hasNormal)
                        {
                            const DirectX::XMVECTOR p0 = DirectX::XMLoadFloat3(&faceVerts[0].Position);
                            const DirectX::XMVECTOR p1 = DirectX::XMLoadFloat3(&faceVerts[1].Position);
                            const DirectX::XMVECTOR p2 = DirectX::XMLoadFloat3(&faceVerts[2].Position);
                            const DirectX::XMVECTOR fn = DirectX::XMVector3Normalize(
                                DirectX::XMVector3Cross(DirectX::XMVectorSubtract(p1, p0), DirectX::XMVectorSubtract(p2, p0)));
                            DirectX::XMFLOAT3 faceN{ 0.0f, 1.0f, 0.0f };
                            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(fn)) > 1e-6f)
                            {
                                DirectX::XMStoreFloat3(&faceN, fn);
                            }
                            for (auto& fv : faceVerts)
                            {
                                fv.Normal = faceN;
                            }
                        }

                        for (auto& fv : faceVerts)
                        {
                            Detail::ComputeOrthogonalTangent(fv);
                        }

                        const u32 baseIdx = static_cast<u32>(outAsset.vertices.size());
                        for (const auto& fv : faceVerts)
                        {
                            outAsset.vertices.push_back(fv);
                        }
                        for (usize k = 1; k + 1 < faceVerts.size(); ++k)
                        {
                            outAsset.indices.push_back(baseIdx);
                            outAsset.indices.push_back(baseIdx + static_cast<u32>(k));
                            outAsset.indices.push_back(baseIdx + static_cast<u32>(k + 1));
                        }
                    }
                }
            }

            if (outAsset.vertices.empty())
            {
                return false;
            }
            outAsset.ComputeBounds();
            return true;
        }

        static bool LoadRLMesh(const std::filesystem::path& realPath, MeshAsset& outAsset)
        {
            std::ifstream file(realPath, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                return false;
            }

            const std::streamsize size = file.tellg();
            if (size <= 0)
            {
                return false;
            }
            file.seekg(0, std::ios::beg);

            std::vector<u8> bytes(static_cast<usize>(size));
            if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
            {
                return false;
            }

            if (bytes.size() >= 16 &&
                bytes[0] == 'R' && bytes[1] == 'M' && bytes[2] == 'S' && bytes[3] == 'H')
            {
                u32 version = 0;
                u32 vertCount = 0;
                u32 idxCount = 0;
                std::memcpy(&version, bytes.data() + 4, sizeof(u32));
                std::memcpy(&vertCount, bytes.data() + 8, sizeof(u32));
                std::memcpy(&idxCount, bytes.data() + 12, sizeof(u32));

                const usize requiredBytes = 16 + static_cast<usize>(vertCount) * sizeof(MeshVertex) + static_cast<usize>(idxCount) * sizeof(u32);
                if (version == 1 && vertCount > 0 && bytes.size() >= requiredBytes)
                {
                    outAsset.vertices.resize(vertCount);
                    outAsset.indices.resize(idxCount);
                    std::memcpy(outAsset.vertices.data(), bytes.data() + 16, static_cast<usize>(vertCount) * sizeof(MeshVertex));
                    if (idxCount > 0)
                    {
                        std::memcpy(outAsset.indices.data(), bytes.data() + 16 + static_cast<usize>(vertCount) * sizeof(MeshVertex), static_cast<usize>(idxCount) * sizeof(u32));
                    }
                    outAsset.ComputeBounds();
                    return true;
                }
            }

            std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '\t'))
            {
                text.pop_back();
            }

            const std::string lower = Detail::ToLowerExt(text);
            if (lower == "cube" || lower == "sphere" || lower == "plane" || lower == "grid" || lower == "quad")
            {
                Detail::BuildPrimitiveVertices(lower, outAsset.vertices, outAsset.indices);
                outAsset.ComputeBounds();
                return !outAsset.vertices.empty();
            }

            return false;
        }
    };

    class TextureAssetImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] bool Handles(std::string_view extension) const override
        {
            const std::string ext = Detail::ToLowerExt(extension);
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
        }

        std::shared_ptr<AssetBase> Import(const std::string&,
                                          const std::filesystem::path& realPath) override
        {
            if (!std::filesystem::exists(realPath))
            {
                return nullptr;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* data = stbi_load(realPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (!data || width <= 0 || height <= 0)
            {
                if (data) stbi_image_free(data);
                RL_LOG_ERROR("TextureAssetImporter: failed to load '{}'", realPath.string());
                return nullptr;
            }

            auto asset = std::make_shared<TextureAsset>();
            asset->width = static_cast<u32>(width);
            asset->height = static_cast<u32>(height);
            asset->channels = static_cast<u32>(channels);
            asset->sourcePath = realPath.string();
            const usize byteCount = static_cast<usize>(width) * static_cast<usize>(height) * 4;
            asset->pixels.assign(data, data + byteCount);
            stbi_image_free(data);
            return asset;
        }
    };

    class ScriptAssetImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] bool Handles(std::string_view extension) const override
        {
            return Detail::ToLowerExt(extension) == ".lua";
        }

        std::shared_ptr<AssetBase> Import(const std::string&,
                                          const std::filesystem::path& realPath) override
        {
            std::ifstream file(realPath, std::ios::binary);
            if (!file.is_open())
            {
                return nullptr;
            }

            auto asset = std::make_shared<ScriptAsset>();
            asset->source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return asset;
        }
    };

    inline void AssetManager::RegisterDefaultImporters()
    {
        std::lock_guard lock(s_mutex);
        if (s_defaultsRegistered)
        {
            return;
        }
        s_defaultsRegistered = true;
        s_importers.push_back(std::make_unique<MeshAssetImporter>());
        s_importers.push_back(std::make_unique<TextureAssetImporter>());
        s_importers.push_back(std::make_unique<ScriptAssetImporter>());
    }
}
