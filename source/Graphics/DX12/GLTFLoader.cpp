#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "Graphics/DX12/GLTFLoader.hpp"
#include "Core/Logger.hpp"

#include <filesystem>
#include <format>
#include <unordered_map>
#include <algorithm>
#include <cstring>

namespace RL
{
    std::expected<ModelData, std::string> GLTFLoader::LoadFromFile(
        ID3D12Device* device,
        std::string_view filepath)
    {
        if (!device)
        {
            return std::unexpected("Device pointer is null when loading glTF.");
        }

        const std::string pathStr(filepath);
        if (!std::filesystem::exists(pathStr))
        {
            return std::unexpected(std::format("glTF file does not exist: '{}'", pathStr));
        }

        cgltf_options options{};
        cgltf_data* data = nullptr;

        cgltf_result parseRes = cgltf_parse_file(&options, pathStr.c_str(), &data);
        if (parseRes != cgltf_result_success || !data)
        {
            return std::unexpected(std::format("cgltf failed to parse file '{}': error code {}", pathStr, static_cast<int>(parseRes)));
        }

        cgltf_result bufRes = cgltf_load_buffers(&options, data, pathStr.c_str());
        if (bufRes != cgltf_result_success)
        {
            cgltf_free(data);
            return std::unexpected(std::format("cgltf failed to load buffers for '{}': error code {}", pathStr, static_cast<int>(bufRes)));
        }

        ModelData modelData;
        modelData.Name = std::filesystem::path(pathStr).stem().string();

        const std::filesystem::path modelDir = std::filesystem::path(pathStr).parent_path();

        for (cgltf_size m = 0; m < data->meshes_count; ++m)
        {
            const cgltf_mesh& gltfMesh = data->meshes[m];
            const std::string meshName = gltfMesh.name ? gltfMesh.name : std::format("{}_Mesh{}", modelData.Name, m);

            for (cgltf_size p = 0; p < gltfMesh.primitives_count; ++p)
            {
                const cgltf_primitive& prim = gltfMesh.primitives[p];

                const cgltf_accessor* posAccessor = nullptr;
                const cgltf_accessor* normAccessor = nullptr;
                const cgltf_accessor* uvAccessor = nullptr;
                const cgltf_accessor* colAccessor = nullptr;
                const cgltf_accessor* tangentAccessor = nullptr;
                const cgltf_accessor* jointsAccessor = nullptr;
                const cgltf_accessor* weightsAccessor = nullptr;

                for (cgltf_size a = 0; a < prim.attributes_count; ++a)
                {
                    const cgltf_attribute& attr = prim.attributes[a];
                    if (attr.type == cgltf_attribute_type_position)
                    {
                        posAccessor = attr.data;
                    }
                    else if (attr.type == cgltf_attribute_type_normal)
                    {
                        normAccessor = attr.data;
                    }
                    else if (attr.type == cgltf_attribute_type_tangent)
                    {
                        tangentAccessor = attr.data;
                    }
                    else if (attr.type == cgltf_attribute_type_texcoord)
                    {
                        uvAccessor = attr.data;
                    }
                    else if (attr.type == cgltf_attribute_type_color)
                    {
                        colAccessor = attr.data;
                    }
                    else if (attr.type == cgltf_attribute_type_joints)
                    {
                        jointsAccessor = attr.data;
                    }
                    else if (attr.type == cgltf_attribute_type_weights)
                    {
                        weightsAccessor = attr.data;
                    }
                }

                if (!posAccessor || posAccessor->count == 0)
                {
                    continue;
                }

                const cgltf_size vertCount = posAccessor->count;
                std::vector<MeshVertex> vertices(vertCount);

                for (cgltf_size i = 0; i < vertCount; ++i)
                {
                    float pos[3]{ 0.0f, 0.0f, 0.0f };
                    cgltf_accessor_read_float(posAccessor, i, pos, 3);
                    vertices[i].Position = { pos[0], pos[1], pos[2] };

                    if (normAccessor)
                    {
                        float norm[3]{ 0.0f, 1.0f, 0.0f };
                        cgltf_accessor_read_float(normAccessor, i, norm, 3);
                        vertices[i].Normal = { norm[0], norm[1], norm[2] };
                    }
                    else
                    {
                        vertices[i].Normal = { 0.0f, 1.0f, 0.0f };
                    }

                    if (uvAccessor)
                    {
                        float uv[2]{ 0.0f, 0.0f };
                        cgltf_accessor_read_float(uvAccessor, i, uv, 2);
                        vertices[i].UV = { uv[0], uv[1] };
                    }
                    else
                    {
                        vertices[i].UV = { 0.0f, 0.0f };
                    }

                    if (colAccessor)
                    {
                        float col[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
                        cgltf_accessor_read_float(colAccessor, i, col, 4);
                        vertices[i].Color = { col[0], col[1], col[2], col[3] };
                    }
                    else
                    {
                        vertices[i].Color = { 1.0f, 1.0f, 1.0f, 1.0f };
                    }

                    if (tangentAccessor)
                    {
                        float tang[4]{ 1.0f, 0.0f, 0.0f, 1.0f };
                        cgltf_accessor_read_float(tangentAccessor, i, tang, 4);
                        vertices[i].Tangent = { tang[0], tang[1], tang[2], tang[3] };
                    }
                    else
                    {
                        const DirectX::XMFLOAT3 n = vertices[i].Normal;
                        const DirectX::XMFLOAT3 t = (std::abs(n.x) > 0.9f) ? DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f) : DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f);
                        const DirectX::XMVECTOR vn = DirectX::XMLoadFloat3(&n);
                        const DirectX::XMVECTOR vt = DirectX::XMLoadFloat3(&t);
                        const DirectX::XMVECTOR dot = DirectX::XMVector3Dot(vn, vt);
                        const DirectX::XMVECTOR ortho = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(vt, DirectX::XMVectorMultiply(vn, dot)));
                        DirectX::XMFLOAT3 normT;
                        DirectX::XMStoreFloat3(&normT, ortho);
                        vertices[i].Tangent = { normT.x, normT.y, normT.z, 1.0f };
                    }

                    if (jointsAccessor)
                    {
                        cgltf_uint joints[4]{ 0, 0, 0, 0 };
                        cgltf_accessor_read_uint(jointsAccessor, i, joints, 4);
                        vertices[i].BoneIndices = { static_cast<u32>(joints[0]), static_cast<u32>(joints[1]), static_cast<u32>(joints[2]), static_cast<u32>(joints[3]) };
                    }

                    if (weightsAccessor)
                    {
                        float weights[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                        cgltf_accessor_read_float(weightsAccessor, i, weights, 4);
                        vertices[i].BoneWeights = { weights[0], weights[1], weights[2], weights[3] };
                    }
                }

                std::vector<u32> indices;
                if (prim.indices)
                {
                    const cgltf_size idxCount = prim.indices->count;
                    indices.resize(idxCount);
                    for (cgltf_size i = 0; i < idxCount; ++i)
                    {
                        const cgltf_size rawIdx = cgltf_accessor_read_index(prim.indices, i);
                        indices[i] = (rawIdx < vertCount) ? static_cast<u32>(rawIdx) : 0u;
                    }
                }
                else
                {
                    indices.resize(vertCount);
                    for (cgltf_size i = 0; i < vertCount; ++i)
                    {
                        indices[i] = static_cast<u32>(i);
                    }
                }

                if (!tangentAccessor && indices.size() >= 3)
                {
                    std::vector<DirectX::XMFLOAT3> tan1(vertCount, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
                    std::vector<DirectX::XMFLOAT3> tan2(vertCount, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));

                    for (usize a = 0; a + 2 < indices.size(); a += 3)
                    {
                        const u32 i1 = indices[a];
                        const u32 i2 = indices[a + 1];
                        const u32 i3 = indices[a + 2];

                        if (i1 >= vertCount || i2 >= vertCount || i3 >= vertCount)
                        {
                            continue;
                        }

                        const auto& v1 = vertices[i1];
                        const auto& v2 = vertices[i2];
                        const auto& v3 = vertices[i3];

                        const float x1 = v2.Position.x - v1.Position.x;
                        const float x2 = v3.Position.x - v1.Position.x;
                        const float y1 = v2.Position.y - v1.Position.y;
                        const float y2 = v3.Position.y - v1.Position.y;
                        const float z1 = v2.Position.z - v1.Position.z;
                        const float z2 = v3.Position.z - v1.Position.z;

                        const float s1 = v2.UV.x - v1.UV.x;
                        const float s2 = v3.UV.x - v1.UV.x;
                        const float t1 = v2.UV.y - v1.UV.y;
                        const float t2 = v3.UV.y - v1.UV.y;

                        const float div = (s1 * t2 - s2 * t1);
                        const float r = (std::abs(div) > 1e-6f) ? (1.0f / div) : 0.0f;

                        const DirectX::XMFLOAT3 sdir{
                            (t2 * x1 - t1 * x2) * r,
                            (t2 * y1 - t1 * y2) * r,
                            (t2 * z1 - t1 * z2) * r
                        };
                        const DirectX::XMFLOAT3 tdir{
                            (s1 * x2 - s2 * x1) * r,
                            (s1 * y2 - s2 * y1) * r,
                            (s1 * z2 - s2 * z1) * r
                        };

                        tan1[i1].x += sdir.x; tan1[i1].y += sdir.y; tan1[i1].z += sdir.z;
                        tan1[i2].x += sdir.x; tan1[i2].y += sdir.y; tan1[i2].z += sdir.z;
                        tan1[i3].x += sdir.x; tan1[i3].y += sdir.y; tan1[i3].z += sdir.z;

                        tan2[i1].x += tdir.x; tan2[i1].y += tdir.y; tan2[i1].z += tdir.z;
                        tan2[i2].x += tdir.x; tan2[i2].y += tdir.y; tan2[i2].z += tdir.z;
                        tan2[i3].x += tdir.x; tan2[i3].y += tdir.y; tan2[i3].z += tdir.z;
                    }

                    for (cgltf_size i = 0; i < vertCount; ++i)
                    {
                        const DirectX::XMVECTOR n = DirectX::XMLoadFloat3(&vertices[i].Normal);
                        const DirectX::XMVECTOR t = DirectX::XMLoadFloat3(&tan1[i]);
                        const DirectX::XMVECTOR t2 = DirectX::XMLoadFloat3(&tan2[i]);

                        const DirectX::XMVECTOR dotNT = DirectX::XMVector3Dot(n, t);
                        DirectX::XMVECTOR orthoT = DirectX::XMVectorSubtract(t, DirectX::XMVectorMultiply(n, dotNT));
                        const float lenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(orthoT));
                        if (lenSq > 1e-6f)
                        {
                            orthoT = DirectX::XMVector3Normalize(orthoT);
                            const DirectX::XMVECTOR crossNT = DirectX::XMVector3Cross(n, t);
                            const float handedness = (DirectX::XMVectorGetX(DirectX::XMVector3Dot(crossNT, t2)) < 0.0f) ? -1.0f : 1.0f;
                            DirectX::XMFLOAT3 normT;
                            DirectX::XMStoreFloat3(&normT, orthoT);
                            vertices[i].Tangent = { normT.x, normT.y, normT.z, handedness };
                        }
                        else
                        {
                            const DirectX::XMFLOAT3 normalF = vertices[i].Normal;
                            const DirectX::XMFLOAT3 fallbackT = (std::abs(normalF.x) > 0.9f) ? DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f) : DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f);
                            const DirectX::XMVECTOR fn = DirectX::XMLoadFloat3(&normalF);
                            const DirectX::XMVECTOR ft = DirectX::XMLoadFloat3(&fallbackT);
                            const DirectX::XMVECTOR fdot = DirectX::XMVector3Dot(fn, ft);
                            const DirectX::XMVECTOR fortho = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(ft, DirectX::XMVectorMultiply(fn, fdot)));
                            DirectX::XMFLOAT3 normT;
                            DirectX::XMStoreFloat3(&normT, fortho);
                            vertices[i].Tangent = { normT.x, normT.y, normT.z, 1.0f };
                        }
                    }
                }

                auto meshRes = Mesh::CreateFromData(device, vertices, indices, meshName);
                if (meshRes)
                {
                    modelData.Meshes.push_back(std::move(*meshRes));
                }

                if (prim.material)
                {
                    auto resolveTextureUri = [&](const char* uri) -> std::string {
                        if (!uri) return "";
                        std::string uriStr(uri);
                        if (uriStr.find(':') != std::string::npos) return "";
                        std::error_code ec;
                        const std::filesystem::path fullTexPath = std::filesystem::weakly_canonical(modelDir / uri, ec);
                        const auto rel = fullTexPath.lexically_relative(modelDir);
                        const std::string relStr = rel.empty() ? "" : rel.string();
                        if (!ec && !relStr.starts_with("..") && !rel.is_absolute())
                        {
                            return fullTexPath.string();
                        }
                        return "";
                    };

                    if (prim.material->has_pbr_metallic_roughness)
                    {
                        const auto& pbr = prim.material->pbr_metallic_roughness;
                        if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
                        {
                            std::string tex = resolveTextureUri(pbr.base_color_texture.texture->image->uri);
                            if (!tex.empty())
                            {
                                modelData.AlbedoTexturePath = tex;
                                modelData.TexturePaths.push_back(tex);
                            }
                        }
                        if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image)
                        {
                            std::string tex = resolveTextureUri(pbr.metallic_roughness_texture.texture->image->uri);
                            if (!tex.empty())
                            {
                                modelData.MetallicRoughnessMapPath = tex;
                            }
                        }
                        modelData.Metallic = pbr.metallic_factor;
                        modelData.Roughness = pbr.roughness_factor;
                    }

                    if (prim.material->normal_texture.texture && prim.material->normal_texture.texture->image)
                    {
                        std::string tex = resolveTextureUri(prim.material->normal_texture.texture->image->uri);
                        if (!tex.empty())
                        {
                            modelData.NormalMapPath = tex;
                        }
                    }

                    if (prim.material->emissive_texture.texture && prim.material->emissive_texture.texture->image)
                    {
                        std::string tex = resolveTextureUri(prim.material->emissive_texture.texture->image->uri);
                        if (!tex.empty())
                        {
                            modelData.EmissiveMapPath = tex;
                        }
                    }

                    modelData.EmissiveColor = {
                        prim.material->emissive_factor[0],
                        prim.material->emissive_factor[1],
                        prim.material->emissive_factor[2]
                    };
                }
            }
        }

        std::unordered_map<const cgltf_node*, i32> globalNodeToBoneIndex;

        for (cgltf_size s = 0; s < data->skins_count; ++s)
        {
            const cgltf_skin& gltfSkin = data->skins[s];
            Skeleton skeleton;
            skeleton.Name = gltfSkin.name ? gltfSkin.name : std::format("{}_Skin{}", modelData.Name, s);
            skeleton.Bones.resize(gltfSkin.joints_count);
            skeleton.SkinMatrices.resize(gltfSkin.joints_count);

            std::unordered_map<const cgltf_node*, i32> skinNodeToBoneIndex;
            skinNodeToBoneIndex.reserve(gltfSkin.joints_count);
            for (cgltf_size j = 0; j < gltfSkin.joints_count; ++j)
            {
                if (gltfSkin.joints[j])
                {
                    const i32 boneIdx = static_cast<i32>(j);
                    skinNodeToBoneIndex[gltfSkin.joints[j]] = boneIdx;
                    if (s == 0)
                    {
                        globalNodeToBoneIndex[gltfSkin.joints[j]] = boneIdx;
                    }
                }
            }

            std::vector<float> ibm(gltfSkin.joints_count * 16, 0.0f);
            if (gltfSkin.inverse_bind_matrices)
            {
                for (cgltf_size j = 0; j < gltfSkin.joints_count; ++j)
                {
                    cgltf_accessor_read_float(gltfSkin.inverse_bind_matrices, j, &ibm[j * 16], 16);
                }
            }

            for (cgltf_size j = 0; j < gltfSkin.joints_count; ++j)
            {
                const cgltf_node* jointNode = gltfSkin.joints[j];
                BoneInfo& bone = skeleton.Bones[j];
                bone.Name = (jointNode && jointNode->name) ? jointNode->name : std::format("Bone_{}", j);

                i32 parentIndex = -1;
                const cgltf_node* parentNode = jointNode ? jointNode->parent : nullptr;
                while (parentNode)
                {
                    const auto it = skinNodeToBoneIndex.find(parentNode);
                    if (it != skinNodeToBoneIndex.end())
                    {
                        parentIndex = it->second;
                        break;
                    }
                    parentNode = parentNode->parent;
                }
                bone.ParentIndex = parentIndex;

                if (gltfSkin.inverse_bind_matrices)
                {
                    std::memcpy(&bone.InverseBindMatrix, &ibm[j * 16], sizeof(DirectX::XMFLOAT4X4));
                }
                else
                {
                    DirectX::XMStoreFloat4x4(&bone.InverseBindMatrix, DirectX::XMMatrixIdentity());
                }

                if (jointNode)
                {
                    float localMat[16]{};
                    cgltf_node_transform_local(jointNode, localMat);
                    std::memcpy(&bone.LocalTransform, localMat, sizeof(DirectX::XMFLOAT4X4));
                }
                else
                {
                    DirectX::XMStoreFloat4x4(&bone.LocalTransform, DirectX::XMMatrixIdentity());
                }

                DirectX::XMStoreFloat4x4(&bone.FinalMatrix, DirectX::XMMatrixIdentity());
                skeleton.SkinMatrices[j] = bone.FinalMatrix;
            }
            modelData.Skeletons.push_back(std::move(skeleton));
        }

        for (cgltf_size a = 0; a < data->animations_count; ++a)
        {
            const cgltf_animation& gltfAnim = data->animations[a];
            AnimationClip clip;
            clip.Name = gltfAnim.name ? gltfAnim.name : std::format("Anim_{}", a);

            std::unordered_map<const cgltf_node*, usize> nodeToTrackIndex;

            for (cgltf_size c = 0; c < gltfAnim.channels_count; ++c)
            {
                const cgltf_animation_channel& ch = gltfAnim.channels[c];
                if (!ch.target_node || !ch.sampler || !ch.sampler->input || !ch.sampler->output)
                {
                    continue;
                }

                if (ch.target_path != cgltf_animation_path_type_translation &&
                    ch.target_path != cgltf_animation_path_type_rotation &&
                    ch.target_path != cgltf_animation_path_type_scale)
                {
                    continue;
                }

                usize trackIdx = 0;
                if (const auto it = nodeToTrackIndex.find(ch.target_node); it != nodeToTrackIndex.end())
                {
                    trackIdx = it->second;
                }
                else
                {
                    AnimationTrack newTrack;
                    if (const auto boneIt = globalNodeToBoneIndex.find(ch.target_node); boneIt != globalNodeToBoneIndex.end())
                    {
                        newTrack.BoneIndex = boneIt->second;
                    }
                    if (ch.target_node->name)
                    {
                        newTrack.BoneName = ch.target_node->name;
                    }
                    else if (newTrack.BoneIndex >= 0)
                    {
                        newTrack.BoneName = std::format("Bone_{}", newTrack.BoneIndex);
                    }
                    trackIdx = clip.Tracks.size();
                    nodeToTrackIndex[ch.target_node] = trackIdx;
                    clip.Tracks.push_back(std::move(newTrack));
                }

                AnimationTrack& track = clip.Tracks[trackIdx];
                const cgltf_size inputCount = ch.sampler->input->count;
                const cgltf_size outputCount = ch.sampler->output->count;
                const bool isCubic = (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline);
                const cgltf_size maxKeysFromOutput = isCubic ? (outputCount / 3) : outputCount;
                const cgltf_size keyCount = (std::min)(inputCount, maxKeysFromOutput);

                if (keyCount == 0)
                {
                    continue;
                }

                std::vector<f32> timestamps(keyCount);
                for (cgltf_size k = 0; k < keyCount; ++k)
                {
                    cgltf_accessor_read_float(ch.sampler->input, k, &timestamps[k], 1);
                    if (timestamps[k] > clip.Duration)
                    {
                        clip.Duration = timestamps[k];
                    }
                }

                if (track.Timestamps.empty())
                {
                    track.Timestamps = timestamps;
                }

                if (ch.target_path == cgltf_animation_path_type_translation)
                {
                    track.PositionTimestamps = std::move(timestamps);
                    track.Positions.resize(keyCount);
                    for (cgltf_size k = 0; k < keyCount; ++k)
                    {
                        const cgltf_size outIdx = isCubic ? (k * 3 + 1) : k;
                        float val[3]{ 0.0f, 0.0f, 0.0f };
                        cgltf_accessor_read_float(ch.sampler->output, outIdx, val, 3);
                        track.Positions[k] = { val[0], val[1], val[2] };
                    }
                }
                else if (ch.target_path == cgltf_animation_path_type_rotation)
                {
                    track.RotationTimestamps = std::move(timestamps);
                    track.Rotations.resize(keyCount);
                    for (cgltf_size k = 0; k < keyCount; ++k)
                    {
                        const cgltf_size outIdx = isCubic ? (k * 3 + 1) : k;
                        float val[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
                        cgltf_accessor_read_float(ch.sampler->output, outIdx, val, 4);
                        track.Rotations[k] = { val[0], val[1], val[2], val[3] };
                    }
                }
                else if (ch.target_path == cgltf_animation_path_type_scale)
                {
                    track.ScaleTimestamps = std::move(timestamps);
                    track.Scales.resize(keyCount);
                    for (cgltf_size k = 0; k < keyCount; ++k)
                    {
                        const cgltf_size outIdx = isCubic ? (k * 3 + 1) : k;
                        float val[3]{ 1.0f, 1.0f, 1.0f };
                        cgltf_accessor_read_float(ch.sampler->output, outIdx, val, 3);
                        track.Scales[k] = { val[0], val[1], val[2] };
                    }
                }
            }
            modelData.Animations.push_back(std::move(clip));
        }

        cgltf_free(data);

        if (modelData.Meshes.empty())
        {
            return std::unexpected(std::format("No valid renderable meshes found in glTF file '{}'", pathStr));
        }

        RL_LOG_INFO("[GLTFLoader] Loaded model '{}' ({} submeshes) from '{}'.",
            modelData.Name, modelData.Meshes.size(), pathStr);

        return modelData;
    }

    std::expected<std::shared_ptr<Mesh>, std::string> GLTFLoader::LoadSingleMesh(
        ID3D12Device* device,
        std::string_view filepath)
    {
        auto modelRes = LoadFromFile(device, filepath);
        if (!modelRes)
        {
            return std::unexpected(modelRes.error());
        }

        if (modelRes->Meshes.empty())
        {
            return std::unexpected("Loaded model contains zero meshes.");
        }

        return modelRes->Meshes[0];
    }
}
