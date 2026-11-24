#include "Scene/Systems/AnimationSystem.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace RL
{
    namespace
    {
        [[nodiscard]] const std::vector<f32>& SelectTimestamps(
            const std::vector<f32>& channelTimestamps,
            const std::vector<f32>& fallbackTimestamps) noexcept
        {
            return channelTimestamps.empty() ? fallbackTimestamps : channelTimestamps;
        }

        void FindKeyframePair(
            const std::vector<f32>& timestamps,
            usize valueCount,
            f32 time,
            usize& outIdx0,
            usize& outIdx1,
            f32& outFactor) noexcept
        {
            if (valueCount <= 1 || timestamps.empty())
            {
                outIdx0 = 0;
                outIdx1 = 0;
                outFactor = 0.0f;
                return;
            }

            const usize count = (std::min)(timestamps.size(), valueCount);
            if (count <= 1 || time <= timestamps[0])
            {
                outIdx0 = 0;
                outIdx1 = 0;
                outFactor = 0.0f;
                return;
            }

            if (time >= timestamps[count - 1])
            {
                outIdx0 = count - 1;
                outIdx1 = count - 1;
                outFactor = 0.0f;
                return;
            }

            const auto upper = std::upper_bound(timestamps.begin(), timestamps.begin() + count, time);
            const usize idx1 = static_cast<usize>(std::distance(timestamps.begin(), upper));
            const usize idx0 = (idx1 > 0) ? (idx1 - 1) : 0;

            const f32 t0 = timestamps[idx0];
            const f32 t1 = timestamps[idx1];
            const f32 delta = t1 - t0;

            outIdx0 = idx0;
            outIdx1 = idx1;
            outFactor = (delta > 1e-6f) ? std::clamp((time - t0) / delta, 0.0f, 1.0f) : 0.0f;
        }

        [[nodiscard]] DirectX::XMVECTOR SampleVec3(
            const std::vector<DirectX::XMFLOAT3>& values,
            const std::vector<f32>& timestamps,
            f32 time,
            DirectX::XMVECTOR fallback) noexcept
        {
            if (values.empty())
            {
                return fallback;
            }

            usize i0 = 0;
            usize i1 = 0;
            f32 factor = 0.0f;
            FindKeyframePair(timestamps, values.size(), time, i0, i1, factor);

            const DirectX::XMVECTOR v0 = DirectX::XMLoadFloat3(&values[i0]);
            if (i0 == i1)
            {
                return v0;
            }
            const DirectX::XMVECTOR v1 = DirectX::XMLoadFloat3(&values[i1]);
            return DirectX::XMVectorLerp(v0, v1, factor);
        }

        [[nodiscard]] DirectX::XMVECTOR SampleQuat(
            const std::vector<DirectX::XMFLOAT4>& values,
            const std::vector<f32>& timestamps,
            f32 time,
            DirectX::XMVECTOR fallback) noexcept
        {
            if (values.empty())
            {
                return DirectX::XMQuaternionNormalize(fallback);
            }

            usize i0 = 0;
            usize i1 = 0;
            f32 factor = 0.0f;
            FindKeyframePair(timestamps, values.size(), time, i0, i1, factor);

            const DirectX::XMVECTOR q0 = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&values[i0]));
            if (i0 == i1)
            {
                return q0;
            }

            DirectX::XMVECTOR q1 = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&values[i1]));
            if (DirectX::XMVectorGetX(DirectX::XMQuaternionDot(q0, q1)) < 0.0f)
            {
                q1 = DirectX::XMVectorNegate(q1);
            }

            return DirectX::XMQuaternionNormalize(DirectX::XMQuaternionSlerp(q0, q1, factor));
        }

        void FillIdentityPalette(std::vector<DirectX::XMFLOAT4X4>& palette, usize count)
        {
            palette.resize(count);
            DirectX::XMFLOAT4X4 identity{};
            DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
            std::fill(palette.begin(), palette.end(), identity);
        }
    }

    void AnimationSystem::RegisterModelRig(
        std::string_view key,
        const std::vector<Skeleton>& skeletons,
        const std::vector<AnimationClip>& clips)
    {
        if (key.empty())
        {
            return;
        }
        ModelRig rig;
        rig.Skeletons = skeletons;
        rig.Clips = clips;
        m_rigs[std::string(key)] = std::move(rig);
    }

    bool AnimationSystem::HasModelRig(std::string_view key) const
    {
        return m_rigs.find(std::string(key)) != m_rigs.end();
    }

    const ModelRig* AnimationSystem::GetModelRig(std::string_view key) const
    {
        const auto it = m_rigs.find(std::string(key));
        if (it == m_rigs.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    void AnimationSystem::ClearRigs() noexcept
    {
        m_rigs.clear();
    }

    void AnimationSystem::CreateProceduralTestRig(std::string_view key)
    {
        Skeleton skeleton;
        skeleton.Name = "ProceduralTestSkeleton";

        BoneInfo rootBone;
        rootBone.Name = "Root";
        rootBone.ParentIndex = -1;
        DirectX::XMStoreFloat4x4(&rootBone.InverseBindMatrix, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&rootBone.LocalTransform, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&rootBone.FinalMatrix, DirectX::XMMatrixIdentity());

        BoneInfo childBone;
        childBone.Name = "Arm";
        childBone.ParentIndex = 0;
        DirectX::XMStoreFloat4x4(&childBone.InverseBindMatrix, DirectX::XMMatrixTranslation(0.0f, -1.0f, 0.0f));
        DirectX::XMStoreFloat4x4(&childBone.LocalTransform, DirectX::XMMatrixTranslation(0.0f, 1.0f, 0.0f));
        DirectX::XMStoreFloat4x4(&childBone.FinalMatrix, DirectX::XMMatrixIdentity());

        skeleton.Bones.push_back(std::move(rootBone));
        skeleton.Bones.push_back(std::move(childBone));
        skeleton.UpdatePalette();

        AnimationClip clip;
        clip.Name = "TestAnim";
        clip.Duration = 1.0f;

        AnimationTrack rootTrack;
        rootTrack.BoneName = "Root";
        rootTrack.BoneIndex = 0;
        rootTrack.PositionTimestamps = { 0.0f, 1.0f };
        rootTrack.Positions = {
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 2.0f, 0.0f }
        };

        AnimationTrack childTrack;
        childTrack.BoneName = "Arm";
        childTrack.BoneIndex = 1;
        childTrack.RotationTimestamps = { 0.0f, 1.0f };
        DirectX::XMFLOAT4 qStart{};
        DirectX::XMFLOAT4 qEnd{};
        DirectX::XMStoreFloat4(&qStart, DirectX::XMQuaternionIdentity());
        DirectX::XMStoreFloat4(&qEnd, DirectX::XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, DirectX::XM_PIDIV2));
        childTrack.Rotations = { qStart, qEnd };

        clip.Tracks.push_back(std::move(rootTrack));
        clip.Tracks.push_back(std::move(childTrack));

        RegisterModelRig(key, { skeleton }, { clip });
    }

    void AnimationSystem::Update(Scene& scene, f32 dt)
    {
        auto* skinnedPool = scene.GetPool<SkinnedMeshComponent>();
        if (!skinnedPool)
        {
            return;
        }

        auto components = skinnedPool->GetData();
        for (SkinnedMeshComponent& comp : components)
        {
            const ModelRig* rig = GetModelRig(comp.ModelPath);
            if (!rig && !comp.SkeletonName.empty())
            {
                rig = GetModelRig(comp.SkeletonName);
            }

            if (!rig || rig->Skeletons.empty())
            {
                FillIdentityPalette(comp.BonePalette, MaxBones);
                continue;
            }

            const Skeleton* targetSkeleton = &rig->Skeletons[0];
            if (!comp.SkeletonName.empty())
            {
                for (const auto& sk : rig->Skeletons)
                {
                    if (sk.Name == comp.SkeletonName)
                    {
                        targetSkeleton = &sk;
                        break;
                    }
                }
            }

            const usize boneCount = targetSkeleton->Bones.size();
            if (boneCount == 0)
            {
                FillIdentityPalette(comp.BonePalette, MaxBones);
                continue;
            }

            const AnimationClip* activeClip = nullptr;
            if (!comp.CurrentAnimation.empty())
            {
                for (const auto& clip : rig->Clips)
                {
                    if (clip.Name == comp.CurrentAnimation)
                    {
                        activeClip = &clip;
                        break;
                    }
                }
            }

            if (comp.IsPlaying)
            {
                comp.AnimationTime += dt * comp.AnimationSpeed;
            }

            if (activeClip && activeClip->Duration > 0.0f)
            {
                comp.AnimationTime = std::fmod(comp.AnimationTime, activeClip->Duration);
                if (comp.AnimationTime < 0.0f)
                {
                    comp.AnimationTime += activeClip->Duration;
                }
            }
            else if (comp.AnimationTime < 0.0f)
            {
                comp.AnimationTime = 0.0f;
            }

            std::vector<const AnimationTrack*> boneTracks(boneCount, nullptr);
            if (activeClip)
            {
                for (const auto& track : activeClip->Tracks)
                {
                    if (track.BoneIndex >= 0 && static_cast<usize>(track.BoneIndex) < boneCount)
                    {
                        boneTracks[static_cast<usize>(track.BoneIndex)] = &track;
                    }
                    else if (!track.BoneName.empty())
                    {
                        for (usize b = 0; b < boneCount; ++b)
                        {
                            if (targetSkeleton->Bones[b].Name == track.BoneName)
                            {
                                boneTracks[b] = &track;
                                break;
                            }
                        }
                    }
                }
            }

            std::vector<DirectX::XMFLOAT4X4> localMatrices(boneCount);
            for (usize i = 0; i < boneCount; ++i)
            {
                const DirectX::XMMATRIX restLocal = DirectX::XMLoadFloat4x4(&targetSkeleton->Bones[i].LocalTransform);
                const AnimationTrack* track = boneTracks[i];
                if (!track || (track->Positions.empty() && track->Rotations.empty() && track->Scales.empty()))
                {
                    DirectX::XMStoreFloat4x4(&localMatrices[i], restLocal);
                    continue;
                }

                DirectX::XMVECTOR restScale{};
                DirectX::XMVECTOR restRot{};
                DirectX::XMVECTOR restTrans{};
                if (!DirectX::XMMatrixDecompose(&restScale, &restRot, &restTrans, restLocal))
                {
                    restScale = DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
                    restRot = DirectX::XMQuaternionIdentity();
                    restTrans = DirectX::XMVectorZero();
                }

                const DirectX::XMVECTOR scale = SampleVec3(
                    track->Scales,
                    SelectTimestamps(track->ScaleTimestamps, track->Timestamps),
                    comp.AnimationTime,
                    restScale);

                const DirectX::XMVECTOR rot = SampleQuat(
                    track->Rotations,
                    SelectTimestamps(track->RotationTimestamps, track->Timestamps),
                    comp.AnimationTime,
                    restRot);

                const DirectX::XMVECTOR trans = SampleVec3(
                    track->Positions,
                    SelectTimestamps(track->PositionTimestamps, track->Timestamps),
                    comp.AnimationTime,
                    restTrans);

                const DirectX::XMMATRIX localMat =
                    DirectX::XMMatrixScalingFromVector(scale) *
                    DirectX::XMMatrixRotationQuaternion(rot) *
                    DirectX::XMMatrixTranslationFromVector(trans);

                DirectX::XMStoreFloat4x4(&localMatrices[i], localMat);
            }

            std::vector<DirectX::XMFLOAT4X4> worldMatrices(boneCount);
            std::vector<u8> visitState(boneCount, 0);

            auto computeWorld = [&](auto& self, usize idx) -> DirectX::XMMATRIX {
                if (visitState[idx] == 2)
                {
                    return DirectX::XMLoadFloat4x4(&worldMatrices[idx]);
                }
                const DirectX::XMMATRIX localMat = DirectX::XMLoadFloat4x4(&localMatrices[idx]);
                if (visitState[idx] == 1)
                {
                    return localMat;
                }

                visitState[idx] = 1;
                DirectX::XMMATRIX worldMat = localMat;
                const i32 parentIdx = targetSkeleton->Bones[idx].ParentIndex;
                if (parentIdx >= 0 && static_cast<usize>(parentIdx) < boneCount && static_cast<usize>(parentIdx) != idx)
                {
                    const DirectX::XMMATRIX parentWorld = self(self, static_cast<usize>(parentIdx));
                    worldMat = DirectX::XMMatrixMultiply(localMat, parentWorld);
                }

                DirectX::XMStoreFloat4x4(&worldMatrices[idx], worldMat);
                visitState[idx] = 2;
                return worldMat;
            };

            const usize paletteSize = (std::min)(boneCount, MaxBones);
            comp.BonePalette.resize(paletteSize);

            for (usize i = 0; i < paletteSize; ++i)
            {
                const DirectX::XMMATRIX worldBone = computeWorld(computeWorld, i);
                const DirectX::XMMATRIX ibm = DirectX::XMLoadFloat4x4(&targetSkeleton->Bones[i].InverseBindMatrix);
                const DirectX::XMMATRIX finalMat = DirectX::XMMatrixMultiply(ibm, worldBone);
                DirectX::XMStoreFloat4x4(&comp.BonePalette[i], finalMat);
            }
        }
    }
}
