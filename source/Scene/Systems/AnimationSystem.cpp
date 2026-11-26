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

        struct BoneTRS
        {
            DirectX::XMVECTOR Scale;
            DirectX::XMVECTOR Rotation;
            DirectX::XMVECTOR Translation;
        };

        [[nodiscard]] const AnimationClip* FindClip(const ModelRig& rig, std::string_view name) noexcept
        {
            if (name.empty())
            {
                return nullptr;
            }
            for (const auto& clip : rig.Clips)
            {
                if (clip.Name == name)
                {
                    return &clip;
                }
            }
            return nullptr;
        }

        [[nodiscard]] f32 NormalizeClipTime(f32 time, const AnimationClip* clip, bool loop) noexcept
        {
            if (clip && clip->Duration > 0.0f)
            {
                if (loop)
                {
                    f32 wrapped = std::fmod(time, clip->Duration);
                    if (wrapped < 0.0f)
                    {
                        wrapped += clip->Duration;
                    }
                    return wrapped;
                }
                return std::clamp(time, 0.0f, clip->Duration);
            }
            return (time < 0.0f) ? 0.0f : time;
        }

        void MapClipTracks(
            const AnimationClip* clip,
            const Skeleton& skeleton,
            std::vector<const AnimationTrack*>& outTracks)
        {
            const usize boneCount = skeleton.Bones.size();
            outTracks.assign(boneCount, nullptr);
            if (!clip)
            {
                return;
            }
            for (const auto& track : clip->Tracks)
            {
                if (track.BoneIndex >= 0 && static_cast<usize>(track.BoneIndex) < boneCount)
                {
                    outTracks[static_cast<usize>(track.BoneIndex)] = &track;
                }
                else if (!track.BoneName.empty())
                {
                    for (usize b = 0; b < boneCount; ++b)
                    {
                        if (skeleton.Bones[b].Name == track.BoneName)
                        {
                            outTracks[b] = &track;
                            break;
                        }
                    }
                }
            }
        }

        [[nodiscard]] bool HasTrackKeyframes(const AnimationTrack* track) noexcept
        {
            return track && (!track->Positions.empty() || !track->Rotations.empty() || !track->Scales.empty());
        }

        [[nodiscard]] BoneTRS SampleBoneTRS(
            const AnimationTrack* track,
            f32 time,
            const DirectX::XMVECTOR& restScale,
            const DirectX::XMVECTOR& restRot,
            const DirectX::XMVECTOR& restTrans) noexcept
        {
            if (!HasTrackKeyframes(track))
            {
                return { restScale, DirectX::XMQuaternionNormalize(restRot), restTrans };
            }

            const DirectX::XMVECTOR scale = SampleVec3(
                track->Scales,
                SelectTimestamps(track->ScaleTimestamps, track->Timestamps),
                time,
                restScale);

            const DirectX::XMVECTOR rot = SampleQuat(
                track->Rotations,
                SelectTimestamps(track->RotationTimestamps, track->Timestamps),
                time,
                restRot);

            const DirectX::XMVECTOR trans = SampleVec3(
                track->Positions,
                SelectTimestamps(track->PositionTimestamps, track->Timestamps),
                time,
                restTrans);

            return { scale, rot, trans };
        }

        [[nodiscard]] DirectX::XMVECTOR ShortestArcQuat(
            DirectX::XMVECTOR fromDir,
            DirectX::XMVECTOR toDir) noexcept
        {
            const DirectX::XMVECTOR u = DirectX::XMVector3Normalize(fromDir);
            const DirectX::XMVECTOR v = DirectX::XMVector3Normalize(toDir);
            const f32 dot = std::clamp(DirectX::XMVectorGetX(DirectX::XMVector3Dot(u, v)), -1.0f, 1.0f);

            if (dot > 0.99999f)
            {
                return DirectX::XMQuaternionIdentity();
            }

            if (dot < -0.99999f)
            {
                DirectX::XMVECTOR ortho = DirectX::XMVector3Cross(u, DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
                if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(ortho)) < 1e-6f)
                {
                    ortho = DirectX::XMVector3Cross(u, DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
                }
                ortho = DirectX::XMVector3Normalize(ortho);
                return DirectX::XMQuaternionRotationNormal(ortho, DirectX::XM_PI);
            }

            const DirectX::XMVECTOR axis = DirectX::XMVector3Cross(u, v);
            const DirectX::XMVECTOR q = DirectX::XMVectorSetW(axis, 1.0f + dot);
            return DirectX::XMQuaternionNormalize(q);
        }

        void SolveTwoBoneIK(
            const Skeleton& skeleton,
            const std::vector<DirectX::XMFLOAT4X4>& localMatrices,
            std::vector<DirectX::XMFLOAT4X4>& worldMatrices,
            std::vector<DirectX::XMFLOAT4X4>& bonePalette,
            usize paletteSize,
            u32 rootIdx,
            u32 midIdx,
            u32 endIdx,
            const DirectX::XMFLOAT3& targetPos,
            f32 weight)
        {
            const usize boneCount = skeleton.Bones.size();
            const DirectX::XMMATRIX rootWorld = DirectX::XMLoadFloat4x4(&worldMatrices[rootIdx]);
            const DirectX::XMMATRIX midWorld = DirectX::XMLoadFloat4x4(&worldMatrices[midIdx]);
            const DirectX::XMMATRIX endWorld = DirectX::XMLoadFloat4x4(&worldMatrices[endIdx]);

            const DirectX::XMVECTOR pRoot = DirectX::XMVectorSetW(rootWorld.r[3], 0.0f);
            const DirectX::XMVECTOR pMid = DirectX::XMVectorSetW(midWorld.r[3], 0.0f);
            const DirectX::XMVECTOR pEnd = DirectX::XMVectorSetW(endWorld.r[3], 0.0f);
            const DirectX::XMVECTOR pTarget = DirectX::XMLoadFloat3(&targetPos);

            const DirectX::XMVECTOR v1 = DirectX::XMVectorSubtract(pMid, pRoot);
            const DirectX::XMVECTOR v2 = DirectX::XMVectorSubtract(pEnd, pMid);
            const f32 L1 = DirectX::XMVectorGetX(DirectX::XMVector3Length(v1));
            const f32 L2 = DirectX::XMVectorGetX(DirectX::XMVector3Length(v2));

            if (L1 < 1e-5f || L2 < 1e-5f)
            {
                return;
            }

            const DirectX::XMVECTOR dir1 = DirectX::XMVectorScale(v1, 1.0f / L1);
            const DirectX::XMVECTOR dir2 = DirectX::XMVectorScale(v2, 1.0f / L2);

            const DirectX::XMVECTOR toTarget = DirectX::XMVectorSubtract(pTarget, pRoot);
            const f32 rawDist = DirectX::XMVectorGetX(DirectX::XMVector3Length(toTarget));

            f32 minDist = std::abs(L1 - L2) + 1e-4f;
            const f32 maxDist = L1 + L2 - 1e-4f;
            if (minDist > maxDist)
            {
                minDist = maxDist;
            }
            const f32 clampedDist = std::clamp(rawDist, minDist, maxDist);

            DirectX::XMVECTOR dirTarget = (rawDist > 1e-5f)
                ? DirectX::XMVectorScale(toTarget, 1.0f / rawDist)
                : dir1;

            DirectX::XMVECTOR bendAxis = DirectX::XMVector3Cross(dir1, dir2);
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(bendAxis)) < 1e-8f)
            {
                bendAxis = DirectX::XMVector3Cross(dir1, dirTarget);
                if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(bendAxis)) < 1e-8f)
                {
                    DirectX::XMVECTOR ref = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                    if (std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(dir1, ref))) > 0.99f)
                    {
                        ref = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
                    }
                    bendAxis = DirectX::XMVector3Cross(dir1, ref);
                }
            }
            bendAxis = DirectX::XMVector3Normalize(bendAxis);

            const f32 cosAlpha = std::clamp(
                (L1 * L1 + clampedDist * clampedDist - L2 * L2) / (2.0f * L1 * clampedDist),
                -1.0f,
                1.0f);
            const f32 alpha = std::acos(cosAlpha);

            const f32 cosBeta = std::clamp(
                (L1 * L1 + L2 * L2 - clampedDist * clampedDist) / (2.0f * L1 * L2),
                -1.0f,
                1.0f);
            const f32 beta = std::acos(cosBeta);

            const DirectX::XMVECTOR qAlpha = DirectX::XMQuaternionRotationNormal(bendAxis, -alpha);
            const DirectX::XMVECTOR newDir1 = DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(dirTarget, qAlpha));

            const DirectX::XMVECTOR qBeta = DirectX::XMQuaternionRotationNormal(bendAxis, DirectX::XM_PI - beta);
            const DirectX::XMVECTOR newDir2 = DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(newDir1, qBeta));

            const f32 clampedWeight = std::clamp(weight, 0.0f, 1.0f);
            const DirectX::XMVECTOR qRootDelta = ShortestArcQuat(dir1, newDir1);
            const DirectX::XMVECTOR qRootBlended = DirectX::XMQuaternionSlerp(
                DirectX::XMQuaternionIdentity(),
                qRootDelta,
                clampedWeight);

            const DirectX::XMVECTOR rotatedDir2 = DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(dir2, qRootBlended));
            const DirectX::XMVECTOR qMidDelta = ShortestArcQuat(rotatedDir2, newDir2);
            const DirectX::XMVECTOR qMidBlended = DirectX::XMQuaternionSlerp(
                DirectX::XMQuaternionIdentity(),
                qMidDelta,
                clampedWeight);

            const DirectX::XMMATRIX rootRotMat =
                DirectX::XMMatrixTranslationFromVector(DirectX::XMVectorNegate(pRoot)) *
                DirectX::XMMatrixRotationQuaternion(qRootBlended) *
                DirectX::XMMatrixTranslationFromVector(pRoot);

            const DirectX::XMMATRIX newRootWorld = DirectX::XMMatrixMultiply(rootWorld, rootRotMat);
            DirectX::XMStoreFloat4x4(&worldMatrices[rootIdx], newRootWorld);

            const DirectX::XMMATRIX midAfterRoot = DirectX::XMMatrixMultiply(midWorld, rootRotMat);
            const DirectX::XMVECTOR newMidPos = DirectX::XMVectorSetW(midAfterRoot.r[3], 0.0f);
            const DirectX::XMMATRIX midRotMat =
                DirectX::XMMatrixTranslationFromVector(DirectX::XMVectorNegate(newMidPos)) *
                DirectX::XMMatrixRotationQuaternion(qMidBlended) *
                DirectX::XMMatrixTranslationFromVector(newMidPos);

            const DirectX::XMMATRIX newMidWorld = DirectX::XMMatrixMultiply(midAfterRoot, midRotMat);
            DirectX::XMStoreFloat4x4(&worldMatrices[midIdx], newMidWorld);

            std::vector<u8> affected(boneCount, 0);
            affected[rootIdx] = 1;
            affected[midIdx] = 1;

            std::vector<u8> updated(boneCount, 0);
            updated[rootIdx] = 1;
            updated[midIdx] = 1;

            auto isDescendantOfIK = [&](auto& self, usize idx) -> bool {
                if (affected[idx])
                {
                    return true;
                }
                const i32 parentIdx = skeleton.Bones[idx].ParentIndex;
                if (parentIdx < 0 || static_cast<usize>(parentIdx) >= boneCount || static_cast<usize>(parentIdx) == idx)
                {
                    return false;
                }
                if (self(self, static_cast<usize>(parentIdx)))
                {
                    affected[idx] = 1;
                    return true;
                }
                return false;
            };

            auto updateWorld = [&](auto& self, usize idx) -> DirectX::XMMATRIX {
                if (updated[idx])
                {
                    return DirectX::XMLoadFloat4x4(&worldMatrices[idx]);
                }
                const i32 parentIdx = skeleton.Bones[idx].ParentIndex;
                if (parentIdx >= 0 && static_cast<usize>(parentIdx) < boneCount && static_cast<usize>(parentIdx) != idx)
                {
                    const DirectX::XMMATRIX parentWorld = self(self, static_cast<usize>(parentIdx));
                    const DirectX::XMMATRIX localMat = DirectX::XMLoadFloat4x4(&localMatrices[idx]);
                    const DirectX::XMMATRIX worldMat = DirectX::XMMatrixMultiply(localMat, parentWorld);
                    DirectX::XMStoreFloat4x4(&worldMatrices[idx], worldMat);
                    updated[idx] = 1;
                    return worldMat;
                }
                updated[idx] = 1;
                return DirectX::XMLoadFloat4x4(&worldMatrices[idx]);
            };

            for (usize i = 0; i < boneCount; ++i)
            {
                if (isDescendantOfIK(isDescendantOfIK, i))
                {
                    updateWorld(updateWorld, i);
                }
            }

            for (usize i = 0; i < paletteSize; ++i)
            {
                if (affected[i])
                {
                    const DirectX::XMMATRIX worldBone = DirectX::XMLoadFloat4x4(&worldMatrices[i]);
                    const DirectX::XMMATRIX ibm = DirectX::XMLoadFloat4x4(&skeleton.Bones[i].InverseBindMatrix);
                    const DirectX::XMMATRIX finalMat = DirectX::XMMatrixMultiply(ibm, worldBone);
                    DirectX::XMStoreFloat4x4(&bonePalette[i], finalMat);
                }
            }
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

        BoneInfo upperArmBone;
        upperArmBone.Name = "UpperArm";
        upperArmBone.ParentIndex = 0;
        DirectX::XMStoreFloat4x4(&upperArmBone.InverseBindMatrix, DirectX::XMMatrixTranslation(0.0f, -1.0f, 0.0f));
        DirectX::XMStoreFloat4x4(&upperArmBone.LocalTransform, DirectX::XMMatrixTranslation(0.0f, 1.0f, 0.0f));
        DirectX::XMStoreFloat4x4(&upperArmBone.FinalMatrix, DirectX::XMMatrixIdentity());

        BoneInfo foreArmBone;
        foreArmBone.Name = "ForeArm";
        foreArmBone.ParentIndex = 1;
        DirectX::XMStoreFloat4x4(&foreArmBone.InverseBindMatrix, DirectX::XMMatrixTranslation(0.0f, -2.0f, 0.0f));
        DirectX::XMStoreFloat4x4(&foreArmBone.LocalTransform, DirectX::XMMatrixTranslation(0.0f, 1.0f, 0.0f));
        DirectX::XMStoreFloat4x4(&foreArmBone.FinalMatrix, DirectX::XMMatrixIdentity());

        skeleton.Bones.push_back(std::move(rootBone));
        skeleton.Bones.push_back(std::move(upperArmBone));
        skeleton.Bones.push_back(std::move(foreArmBone));
        skeleton.UpdatePalette();

        AnimationClip idleClip;
        idleClip.Name = "Idle";
        idleClip.Duration = 2.0f;

        AnimationTrack idleArmTrack;
        idleArmTrack.BoneName = "UpperArm";
        idleArmTrack.BoneIndex = 1;
        idleArmTrack.RotationTimestamps = { 0.0f, 1.0f, 2.0f };
        DirectX::XMFLOAT4 qIdleZero{};
        DirectX::XMFLOAT4 qIdlePeak{};
        DirectX::XMStoreFloat4(&qIdleZero, DirectX::XMQuaternionIdentity());
        DirectX::XMStoreFloat4(
            &qIdlePeak,
            DirectX::XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, DirectX::XMConvertToRadians(15.0f)));
        idleArmTrack.Rotations = { qIdleZero, qIdlePeak, qIdleZero };
        idleClip.Tracks.push_back(std::move(idleArmTrack));

        AnimationClip waveClip;
        waveClip.Name = "Wave";
        waveClip.Duration = 1.0f;

        AnimationTrack waveArmTrack;
        waveArmTrack.BoneName = "UpperArm";
        waveArmTrack.BoneIndex = 1;
        waveArmTrack.RotationTimestamps = { 0.0f, 1.0f };
        DirectX::XMFLOAT4 qWaveStart{};
        DirectX::XMFLOAT4 qWaveEnd{};
        DirectX::XMStoreFloat4(&qWaveStart, DirectX::XMQuaternionIdentity());
        DirectX::XMStoreFloat4(&qWaveEnd, DirectX::XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, DirectX::XM_PIDIV2));
        waveArmTrack.Rotations = { qWaveStart, qWaveEnd };
        waveClip.Tracks.push_back(std::move(waveArmTrack));

        RegisterModelRig(key, { skeleton }, { idleClip, waveClip });
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

            const AnimationClip* activeClip = FindClip(*rig, comp.CurrentAnimation);
            const AnimationClip* prevClip = (!comp.PreviousAnimation.empty() && comp.BlendDuration > 0.0f)
                ? FindClip(*rig, comp.PreviousAnimation)
                : nullptr;

            if (comp.IsPlaying)
            {
                comp.AnimationTime += dt * comp.AnimationSpeed;
                if (prevClip && comp.BlendDuration > 0.0f)
                {
                    comp.PreviousAnimationTime += dt * comp.AnimationSpeed;
                    comp.BlendTime += dt;
                    if (comp.BlendTime >= comp.BlendDuration)
                    {
                        comp.PreviousAnimation.clear();
                        comp.PreviousAnimationTime = 0.0f;
                        comp.BlendTime = 0.0f;
                        comp.BlendDuration = 0.0f;
                        prevClip = nullptr;
                    }
                }
            }

            comp.AnimationTime = NormalizeClipTime(comp.AnimationTime, activeClip, comp.Loop);
            if (prevClip)
            {
                comp.PreviousAnimationTime = NormalizeClipTime(comp.PreviousAnimationTime, prevClip, comp.Loop);
            }

            const bool isBlending = (prevClip != nullptr && !comp.PreviousAnimation.empty() && comp.BlendDuration > 0.0f);
            const f32 blendAlpha = isBlending
                ? std::clamp(comp.BlendTime / comp.BlendDuration, 0.0f, 1.0f)
                : 1.0f;

            std::vector<const AnimationTrack*> boneTracks;
            MapClipTracks(activeClip, *targetSkeleton, boneTracks);

            std::vector<const AnimationTrack*> prevBoneTracks;
            if (isBlending)
            {
                MapClipTracks(prevClip, *targetSkeleton, prevBoneTracks);
            }

            std::vector<DirectX::XMFLOAT4X4> localMatrices(boneCount);
            for (usize i = 0; i < boneCount; ++i)
            {
                const DirectX::XMMATRIX restLocal = DirectX::XMLoadFloat4x4(&targetSkeleton->Bones[i].LocalTransform);
                const AnimationTrack* track = boneTracks[i];
                const AnimationTrack* prevTrack = isBlending ? prevBoneTracks[i] : nullptr;

                if (!HasTrackKeyframes(track) && !HasTrackKeyframes(prevTrack))
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

                BoneTRS finalTRS = SampleBoneTRS(track, comp.AnimationTime, restScale, restRot, restTrans);
                if (isBlending)
                {
                    const BoneTRS prevTRS = SampleBoneTRS(
                        prevTrack,
                        comp.PreviousAnimationTime,
                        restScale,
                        restRot,
                        restTrans);

                    finalTRS.Scale = DirectX::XMVectorLerp(prevTRS.Scale, finalTRS.Scale, blendAlpha);
                    finalTRS.Translation = DirectX::XMVectorLerp(prevTRS.Translation, finalTRS.Translation, blendAlpha);

                    DirectX::XMVECTOR q0 = prevTRS.Rotation;
                    DirectX::XMVECTOR q1 = finalTRS.Rotation;
                    if (DirectX::XMVectorGetX(DirectX::XMQuaternionDot(q0, q1)) < 0.0f)
                    {
                        q1 = DirectX::XMVectorNegate(q1);
                    }
                    finalTRS.Rotation = DirectX::XMQuaternionNormalize(DirectX::XMQuaternionSlerp(q0, q1, blendAlpha));
                }

                const DirectX::XMMATRIX localMat =
                    DirectX::XMMatrixScalingFromVector(finalTRS.Scale) *
                    DirectX::XMMatrixRotationQuaternion(finalTRS.Rotation) *
                    DirectX::XMMatrixTranslationFromVector(finalTRS.Translation);

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

            for (usize i = 0; i < boneCount; ++i)
            {
                computeWorld(computeWorld, i);
            }

            for (usize i = 0; i < paletteSize; ++i)
            {
                const DirectX::XMMATRIX worldBone = DirectX::XMLoadFloat4x4(&worldMatrices[i]);
                const DirectX::XMMATRIX ibm = DirectX::XMLoadFloat4x4(&targetSkeleton->Bones[i].InverseBindMatrix);
                const DirectX::XMMATRIX finalMat = DirectX::XMMatrixMultiply(ibm, worldBone);
                DirectX::XMStoreFloat4x4(&comp.BonePalette[i], finalMat);
            }

            if (comp.EnableIK &&
                comp.IKWeight > 0.001f &&
                comp.IKRootBone < boneCount &&
                comp.IKMidBone < boneCount &&
                comp.IKEndBone < boneCount &&
                comp.IKRootBone != comp.IKMidBone &&
                comp.IKMidBone != comp.IKEndBone &&
                comp.IKRootBone != comp.IKEndBone)
            {
                SolveTwoBoneIK(
                    *targetSkeleton,
                    localMatrices,
                    worldMatrices,
                    comp.BonePalette,
                    paletteSize,
                    comp.IKRootBone,
                    comp.IKMidBone,
                    comp.IKEndBone,
                    comp.IKTargetPos,
                    comp.IKWeight);
            }
        }
    }
}
