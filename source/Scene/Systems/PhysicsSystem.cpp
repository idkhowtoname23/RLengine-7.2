#include "Scene/Systems/PhysicsSystem.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/Events/CommonEvents.hpp"

#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace RL
{
    namespace
    {
        inline u64 HashGridCell(i32 cx, i32 cy, i32 cz) noexcept
        {
            const u64 h1 = static_cast<u64>(static_cast<u32>(cx)) * 73856093ull;
            const u64 h2 = static_cast<u64>(static_cast<u32>(cy)) * 19349663ull;
            const u64 h3 = static_cast<u64>(static_cast<u32>(cz)) * 83492791ull;
            return h1 ^ h2 ^ h3;
        }

        inline f32 Dot(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        inline DirectX::XMFLOAT3 Cross(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
        {
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            };
        }

        inline DirectX::XMFLOAT3 Add(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
        {
            return { a.x + b.x, a.y + b.y, a.z + b.z };
        }

        inline DirectX::XMFLOAT3 Sub(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
        {
            return { a.x - b.x, a.y - b.y, a.z - b.z };
        }

        inline DirectX::XMFLOAT3 Mul(const DirectX::XMFLOAT3& a, f32 s) noexcept
        {
            return { a.x * s, a.y * s, a.z * s };
        }

        struct OBB
        {
            DirectX::XMFLOAT3 Center{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Axes[3]{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
            DirectX::XMFLOAT3 Extents{ 0.5f, 0.5f, 0.5f };
        };

        struct Sphere
        {
            DirectX::XMFLOAT3 Center{ 0.0f, 0.0f, 0.0f };
            f32 Radius = 0.5f;
        };

        struct ContactInfo
        {
            DirectX::XMFLOAT3 Point{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Normal{ 0.0f, 1.0f, 0.0f };
            f32 Penetration = 0.0f;
            bool HasCollision = false;
        };

        struct PhysicsBodyEntry
        {
            Entity Ent{};
            TransformComponent* Transform = nullptr;
            RigidBodyComponent* RigidBody = nullptr;
            const BoxColliderComponent* BoxComp = nullptr;
            const SphereColliderComponent* SphereComp = nullptr;
            bool IsSphere = false;
            bool IsTrigger = false;
            bool IsDynamic = false;
            OBB BoxShape{};
            Sphere SphereShape{};
            f32 BoundingRadius = 0.5f;
            DirectX::XMFLOAT3 Center{ 0.0f, 0.0f, 0.0f };
        };

        inline void AddTriggerPair(std::vector<TriggerPair>& pairs, Entity trig, Entity other)
        {
            for (const auto& p : pairs)
            {
                if (p.TriggerEntity == trig && p.OtherEntity == other)
                {
                    return;
                }
            }
            pairs.push_back(TriggerPair{ trig, other });
        }

        inline OBB BuildOBB(const TransformComponent& tr, const BoxColliderComponent* box) noexcept
        {
            OBB obb;
            const DirectX::XMFLOAT3 halfSize = box ? box->HalfSize : DirectX::XMFLOAT3{ 0.5f, 0.5f, 0.5f };
            const DirectX::XMFLOAT3 offset = box ? box->Offset : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };

            obb.Extents = {
                std::max(1e-4f, std::abs(halfSize.x * tr.Scale.x)),
                std::max(1e-4f, std::abs(halfSize.y * tr.Scale.y)),
                std::max(1e-4f, std::abs(halfSize.z * tr.Scale.z))
            };

            const auto rot = DirectX::XMMatrixRotationRollPitchYaw(tr.Rotation.x, tr.Rotation.y, tr.Rotation.z);
            obb.Axes[0] = { DirectX::XMVectorGetX(rot.r[0]), DirectX::XMVectorGetY(rot.r[0]), DirectX::XMVectorGetZ(rot.r[0]) };
            obb.Axes[1] = { DirectX::XMVectorGetX(rot.r[1]), DirectX::XMVectorGetY(rot.r[1]), DirectX::XMVectorGetZ(rot.r[1]) };
            obb.Axes[2] = { DirectX::XMVectorGetX(rot.r[2]), DirectX::XMVectorGetY(rot.r[2]), DirectX::XMVectorGetZ(rot.r[2]) };

            const DirectX::XMFLOAT3 scaledOffset{
                offset.x * tr.Scale.x,
                offset.y * tr.Scale.y,
                offset.z * tr.Scale.z
            };

            obb.Center = {
                tr.Position.x + obb.Axes[0].x * scaledOffset.x + obb.Axes[1].x * scaledOffset.y + obb.Axes[2].x * scaledOffset.z,
                tr.Position.y + obb.Axes[0].y * scaledOffset.x + obb.Axes[1].y * scaledOffset.y + obb.Axes[2].y * scaledOffset.z,
                tr.Position.z + obb.Axes[0].z * scaledOffset.x + obb.Axes[1].z * scaledOffset.y + obb.Axes[2].z * scaledOffset.z
            };

            return obb;
        }

        inline Sphere BuildSphere(const TransformComponent& tr, const SphereColliderComponent* sphere) noexcept
        {
            Sphere s;
            const f32 baseRadius = sphere ? sphere->Radius : 0.5f;
            const DirectX::XMFLOAT3 offset = sphere ? sphere->Offset : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
            const f32 maxScale = std::max({ std::abs(tr.Scale.x), std::abs(tr.Scale.y), std::abs(tr.Scale.z) });
            s.Radius = std::max(1e-4f, baseRadius * maxScale);
            s.Center = {
                tr.Position.x + offset.x * tr.Scale.x,
                tr.Position.y + offset.y * tr.Scale.y,
                tr.Position.z + offset.z * tr.Scale.z
            };
            return s;
        }

        inline DirectX::XMFLOAT3 TransformInverseInertia(
            const DirectX::XMFLOAT3& torque,
            const DirectX::XMFLOAT3 axes[3],
            const DirectX::XMFLOAT3& extents,
            f32 mass) noexcept
        {
            const f32 m = std::max(0.001f, mass);
            const f32 Ixx = (1.0f / 3.0f) * m * (extents.y * extents.y + extents.z * extents.z);
            const f32 Iyy = (1.0f / 3.0f) * m * (extents.x * extents.x + extents.z * extents.z);
            const f32 Izz = (1.0f / 3.0f) * m * (extents.x * extents.x + extents.y * extents.y);

            const DirectX::XMFLOAT3 invIlocal{
                1.0f / std::max(1e-4f, Ixx),
                1.0f / std::max(1e-4f, Iyy),
                1.0f / std::max(1e-4f, Izz)
            };

            const DirectX::XMFLOAT3 tLoc{
                Dot(torque, axes[0]),
                Dot(torque, axes[1]),
                Dot(torque, axes[2])
            };

            const DirectX::XMFLOAT3 invItLoc{
                tLoc.x * invIlocal.x,
                tLoc.y * invIlocal.y,
                tLoc.z * invIlocal.z
            };

            return {
                axes[0].x * invItLoc.x + axes[1].x * invItLoc.y + axes[2].x * invItLoc.z,
                axes[0].y * invItLoc.x + axes[1].y * invItLoc.y + axes[2].y * invItLoc.z,
                axes[0].z * invItLoc.x + axes[1].z * invItLoc.y + axes[2].z * invItLoc.z
            };
        }

        inline bool SweptAABB(
            const DirectX::XMFLOAT3& P,
            const DirectX::XMFLOAT3& V,
            const DirectX::XMFLOAT3& E,
            f32& outTOI,
            DirectX::XMFLOAT3& outNormal) noexcept
        {
            f32 tNear = 0.0f;
            f32 tFar = 1.0f;
            DirectX::XMFLOAT3 nearNormal{ 0.0f, 0.0f, 0.0f };

            const f32 p[3] = { P.x, P.y, P.z };
            const f32 v[3] = { V.x, V.y, V.z };
            const f32 ext[3] = { E.x, E.y, E.z };

            for (int k = 0; k < 3; ++k)
            {
                if (std::abs(v[k]) < 1e-6f)
                {
                    if (std::abs(p[k]) > ext[k])
                    {
                        return false;
                    }
                }
                else
                {
                    f32 t1 = (-ext[k] - p[k]) / v[k];
                    f32 t2 = (ext[k] - p[k]) / v[k];
                    f32 sign = -1.0f;
                    if (t1 > t2)
                    {
                        std::swap(t1, t2);
                        sign = 1.0f;
                    }

                    if (t1 > tNear)
                    {
                        tNear = t1;
                        nearNormal = { 0.0f, 0.0f, 0.0f };
                        if (k == 0) nearNormal.x = sign;
                        else if (k == 1) nearNormal.y = sign;
                        else nearNormal.z = sign;
                    }
                    if (t2 < tFar)
                    {
                        tFar = t2;
                    }

                    if (tNear > tFar)
                    {
                        return false;
                    }
                }
            }

            if (tNear > 1e-6f && tNear < 1.0f)
            {
                outTOI = tNear;
                outNormal = nearNormal;
                return true;
            }

            return false;
        }

        inline bool SweptBoxOBB(
            const OBB& a,
            const DirectX::XMFLOAT3& relV,
            const OBB& b,
            f32& outTOI,
            DirectX::XMFLOAT3& outNormal) noexcept
        {
            const f32 vLenSq = Dot(relV, relV);
            if (vLenSq < 1e-8f)
            {
                return false;
            }

            const DirectX::XMFLOAT3 extB = b.Extents;
            const DirectX::XMFLOAT3 extAProj{
                a.Extents.x * std::abs(Dot(a.Axes[0], b.Axes[0])) + a.Extents.y * std::abs(Dot(a.Axes[1], b.Axes[0])) + a.Extents.z * std::abs(Dot(a.Axes[2], b.Axes[0])),
                a.Extents.x * std::abs(Dot(a.Axes[0], b.Axes[1])) + a.Extents.y * std::abs(Dot(a.Axes[1], b.Axes[1])) + a.Extents.z * std::abs(Dot(a.Axes[2], b.Axes[1])),
                a.Extents.x * std::abs(Dot(a.Axes[0], b.Axes[2])) + a.Extents.y * std::abs(Dot(a.Axes[1], b.Axes[2])) + a.Extents.z * std::abs(Dot(a.Axes[2], b.Axes[2]))
            };

            const DirectX::XMFLOAT3 totalExt = Add(extB, extAProj);
            const DirectX::XMFLOAT3 deltaC = Sub(a.Center, b.Center);

            const DirectX::XMFLOAT3 localP{
                Dot(deltaC, b.Axes[0]),
                Dot(deltaC, b.Axes[1]),
                Dot(deltaC, b.Axes[2])
            };

            const DirectX::XMFLOAT3 localV{
                Dot(relV, b.Axes[0]),
                Dot(relV, b.Axes[1]),
                Dot(relV, b.Axes[2])
            };

            DirectX::XMFLOAT3 localNorm;
            if (SweptAABB(localP, localV, totalExt, outTOI, localNorm))
            {
                outNormal = Add(
                    Mul(b.Axes[0], localNorm.x),
                    Add(Mul(b.Axes[1], localNorm.y), Mul(b.Axes[2], localNorm.z))
                );
                if (Dot(relV, outNormal) > 0.0f)
                {
                    outNormal = Mul(outNormal, -1.0f);
                }
                return true;
            }

            return false;
        }

        inline bool SweptBoxSphere(
            const OBB& a,
            const DirectX::XMFLOAT3& relV,
            const Sphere& b,
            f32& outTOI,
            DirectX::XMFLOAT3& outNormal) noexcept
        {
            const f32 vLenSq = Dot(relV, relV);
            if (vLenSq < 1e-8f)
            {
                return false;
            }

            const DirectX::XMFLOAT3 totalExt{
                a.Extents.x + b.Radius,
                a.Extents.y + b.Radius,
                a.Extents.z + b.Radius
            };

            const DirectX::XMFLOAT3 deltaC = Sub(b.Center, a.Center);
            const DirectX::XMFLOAT3 invV = Mul(relV, -1.0f);

            const DirectX::XMFLOAT3 localP{
                Dot(deltaC, a.Axes[0]),
                Dot(deltaC, a.Axes[1]),
                Dot(deltaC, a.Axes[2])
            };

            const DirectX::XMFLOAT3 localV{
                Dot(invV, a.Axes[0]),
                Dot(invV, a.Axes[1]),
                Dot(invV, a.Axes[2])
            };

            DirectX::XMFLOAT3 localNorm;
            if (SweptAABB(localP, localV, totalExt, outTOI, localNorm))
            {
                outNormal = Add(
                    Mul(a.Axes[0], localNorm.x),
                    Add(Mul(a.Axes[1], localNorm.y), Mul(a.Axes[2], localNorm.z))
                );
                if (Dot(relV, outNormal) > 0.0f)
                {
                    outNormal = Mul(outNormal, -1.0f);
                }
                return true;
            }

            return false;
        }

        inline bool SweptSphereSphere(
            const DirectX::XMFLOAT3& cA,
            const DirectX::XMFLOAT3& relV,
            f32 rA,
            const DirectX::XMFLOAT3& cB,
            f32 rB,
            f32& outTOI,
            DirectX::XMFLOAT3& outNormal) noexcept
        {
            const f32 vLenSq = Dot(relV, relV);
            if (vLenSq < 1e-8f)
            {
                return false;
            }

            const DirectX::XMFLOAT3 deltaC = Sub(cA, cB);
            const f32 rSum = rA + rB;
            const f32 cDistSq = Dot(deltaC, deltaC);

            if (cDistSq <= rSum * rSum)
            {
                return false;
            }

            const f32 a = vLenSq;
            const f32 b = 2.0f * Dot(deltaC, relV);
            const f32 c = cDistSq - rSum * rSum;

            if (b >= 0.0f)
            {
                return false;
            }

            const f32 disc = b * b - 4.0f * a * c;
            if (disc < 0.0f)
            {
                return false;
            }

            const f32 t = (-b - std::sqrt(disc)) / (2.0f * a);
            if (t >= 0.0f && t < 1.0f)
            {
                outTOI = t;
                const DirectX::XMFLOAT3 hitPosA = Add(cA, Mul(relV, t));
                const DirectX::XMFLOAT3 n = Sub(hitPosA, cB);
                const f32 nLen = std::sqrt(Dot(n, n));
                outNormal = (nLen > 1e-4f) ? Mul(n, 1.0f / nLen) : Mul(relV, -1.0f / std::sqrt(vLenSq));
                if (Dot(relV, outNormal) > 0.0f)
                {
                    outNormal = Mul(outNormal, -1.0f);
                }
                return true;
            }

            return false;
        }

        inline ContactInfo CheckOBBOBB(const OBB& a, const OBB& b) noexcept
        {
            const DirectX::XMFLOAT3 D = Sub(b.Center, a.Center);
            f32 minPen = 1e30f;
            DirectX::XMFLOAT3 bestAxis{ 0.0f, 1.0f, 0.0f };

            auto testAxis = [&](const DirectX::XMFLOAT3& axis) -> bool
            {
                const f32 lenSq = Dot(axis, axis);
                if (lenSq < 1e-8f) return true;

                const f32 invLen = 1.0f / std::sqrt(lenSq);
                const DirectX::XMFLOAT3 normAxis = Mul(axis, invLen);

                const f32 rA = a.Extents.x * std::abs(Dot(a.Axes[0], normAxis)) +
                               a.Extents.y * std::abs(Dot(a.Axes[1], normAxis)) +
                               a.Extents.z * std::abs(Dot(a.Axes[2], normAxis));

                const f32 rB = b.Extents.x * std::abs(Dot(b.Axes[0], normAxis)) +
                               b.Extents.y * std::abs(Dot(b.Axes[1], normAxis)) +
                               b.Extents.z * std::abs(Dot(b.Axes[2], normAxis));

                const f32 dist = std::abs(Dot(D, normAxis));
                const f32 pen = (rA + rB) - dist;

                if (pen <= 0.0f)
                {
                    return false;
                }

                if (pen < minPen)
                {
                    minPen = pen;
                    bestAxis = normAxis;
                    if (Dot(D, bestAxis) < 0.0f)
                    {
                        bestAxis = Mul(bestAxis, -1.0f);
                    }
                }
                return true;
            };

            if (!testAxis(a.Axes[0])) return {};
            if (!testAxis(a.Axes[1])) return {};
            if (!testAxis(a.Axes[2])) return {};

            if (!testAxis(b.Axes[0])) return {};
            if (!testAxis(b.Axes[1])) return {};
            if (!testAxis(b.Axes[2])) return {};

            if (!testAxis(Cross(a.Axes[0], b.Axes[0]))) return {};
            if (!testAxis(Cross(a.Axes[0], b.Axes[1]))) return {};
            if (!testAxis(Cross(a.Axes[0], b.Axes[2]))) return {};

            if (!testAxis(Cross(a.Axes[1], b.Axes[0]))) return {};
            if (!testAxis(Cross(a.Axes[1], b.Axes[1]))) return {};
            if (!testAxis(Cross(a.Axes[1], b.Axes[2]))) return {};

            if (!testAxis(Cross(a.Axes[2], b.Axes[0]))) return {};
            if (!testAxis(Cross(a.Axes[2], b.Axes[1]))) return {};
            if (!testAxis(Cross(a.Axes[2], b.Axes[2]))) return {};

            DirectX::XMFLOAT3 pA = a.Center;
            const f32 aExt[3] = { a.Extents.x, a.Extents.y, a.Extents.z };
            for (int k = 0; k < 3; ++k)
            {
                const f32 sign = (Dot(a.Axes[k], bestAxis) >= 0.0f) ? 1.0f : -1.0f;
                pA = Add(pA, Mul(a.Axes[k], sign * aExt[k]));
            }

            DirectX::XMFLOAT3 pB = b.Center;
            const f32 bExt[3] = { b.Extents.x, b.Extents.y, b.Extents.z };
            for (int k = 0; k < 3; ++k)
            {
                const f32 sign = (Dot(b.Axes[k], bestAxis) <= 0.0f) ? 1.0f : -1.0f;
                pB = Add(pB, Mul(b.Axes[k], sign * bExt[k]));
            }

            ContactInfo info;
            info.HasCollision = true;
            info.Normal = bestAxis;
            info.Penetration = minPen;
            info.Point = Mul(Add(pA, pB), 0.5f);
            return info;
        }

        inline ContactInfo CheckBoxSphere(const OBB& a, const Sphere& b) noexcept
        {
            const DirectX::XMFLOAT3 D = Sub(b.Center, a.Center);
            const f32 d0 = Dot(D, a.Axes[0]);
            const f32 d1 = Dot(D, a.Axes[1]);
            const f32 d2 = Dot(D, a.Axes[2]);

            const f32 c0 = std::clamp(d0, -a.Extents.x, a.Extents.x);
            const f32 c1 = std::clamp(d1, -a.Extents.y, a.Extents.y);
            const f32 c2 = std::clamp(d2, -a.Extents.z, a.Extents.z);

            const DirectX::XMFLOAT3 pClosest = Add(a.Center, Add(Mul(a.Axes[0], c0), Add(Mul(a.Axes[1], c1), Mul(a.Axes[2], c2))));
            const DirectX::XMFLOAT3 V = Sub(b.Center, pClosest);
            const f32 distSq = Dot(V, V);

            if (distSq > 1e-8f)
            {
                if (distSq < b.Radius * b.Radius)
                {
                    const f32 dist = std::sqrt(distSq);
                    ContactInfo info;
                    info.HasCollision = true;
                    info.Normal = Mul(V, 1.0f / dist);
                    info.Penetration = b.Radius - dist;
                    info.Point = pClosest;
                    return info;
                }
                return {};
            }

            const f32 faceDists[6] = {
                a.Extents.x - d0,
                a.Extents.x + d0,
                a.Extents.y - d1,
                a.Extents.y + d1,
                a.Extents.z - d2,
                a.Extents.z + d2
            };

            f32 minFaceDist = faceDists[0];
            int minIdx = 0;
            for (int k = 1; k < 6; ++k)
            {
                if (faceDists[k] < minFaceDist)
                {
                    minFaceDist = faceDists[k];
                    minIdx = k;
                }
            }

            DirectX::XMFLOAT3 N = a.Axes[minIdx / 2];
            if ((minIdx % 2) != 0)
            {
                N = Mul(N, -1.0f);
            }

            ContactInfo info;
            info.HasCollision = true;
            info.Normal = N;
            info.Penetration = b.Radius + minFaceDist;
            info.Point = Sub(b.Center, Mul(N, minFaceDist));
            return info;
        }

        inline ContactInfo CheckSphereSphere(const Sphere& a, const Sphere& b) noexcept
        {
            const DirectX::XMFLOAT3 D = Sub(b.Center, a.Center);
            const f32 distSq = Dot(D, D);
            const f32 minDist = a.Radius + b.Radius;

            if (distSq < minDist * minDist && distSq > 1e-8f)
            {
                const f32 dist = std::sqrt(distSq);
                ContactInfo info;
                info.HasCollision = true;
                info.Normal = Mul(D, 1.0f / dist);
                info.Penetration = minDist - dist;
                info.Point = Add(a.Center, Mul(info.Normal, a.Radius));
                return info;
            }

            return {};
        }
    }

    void PhysicsSystem::ClearSnapshot() noexcept
    {
        m_initialTransforms.clear();
        m_initialRotations.clear();
        m_activeTriggerPairs.clear();
        m_triggerEvents.clear();
        m_accumulator = 0.0f;
    }

    void PhysicsSystem::ClearTriggers() noexcept
    {
        m_activeTriggerPairs.clear();
        m_triggerEvents.clear();
    }

    bool PhysicsSystem::HasTriggerOverlap(Entity triggerEntity, Entity otherEntity) const noexcept
    {
        for (const auto& pair : m_activeTriggerPairs)
        {
            if ((pair.TriggerEntity == triggerEntity && pair.OtherEntity == otherEntity) ||
                (pair.TriggerEntity == otherEntity && pair.OtherEntity == triggerEntity))
            {
                return true;
            }
        }
        return false;
    }

    bool PhysicsSystem::IsTriggerActive(Entity triggerEntity) const noexcept
    {
        for (const auto& pair : m_activeTriggerPairs)
        {
            if (pair.TriggerEntity == triggerEntity || pair.OtherEntity == triggerEntity)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<Entity> PhysicsSystem::GetEntitiesInTrigger(Entity triggerEntity) const
    {
        std::vector<Entity> result;
        for (const auto& pair : m_activeTriggerPairs)
        {
            if (pair.TriggerEntity == triggerEntity)
            {
                if (std::find(result.begin(), result.end(), pair.OtherEntity) == result.end())
                {
                    result.push_back(pair.OtherEntity);
                }
            }
            else if (pair.OtherEntity == triggerEntity)
            {
                if (std::find(result.begin(), result.end(), pair.TriggerEntity) == result.end())
                {
                    result.push_back(pair.TriggerEntity);
                }
            }
        }
        return result;
    }

    void PhysicsSystem::CaptureSnapshot(Scene& scene)
    {
        ClearSnapshot();
        auto* rbPool = scene.GetPool<RigidBodyComponent>();
        auto* trPool = scene.GetPool<TransformComponent>();
        if (!rbPool || !trPool) return;

        for (const auto& ent : scene.GetAliveEntities())
        {
            if (rbPool->Has(ent.GetIndex()))
            {
                if (const auto* tr = trPool->TryGet(ent.GetIndex()))
                {
                    m_initialTransforms[ent.GetID()] = tr->Position;
                    m_initialRotations[ent.GetID()] = tr->Rotation;
                }
            }
        }
    }

    void PhysicsSystem::RestoreSnapshot(Scene& scene)
    {
        auto* rbPool = scene.GetPool<RigidBodyComponent>();
        auto* trPool = scene.GetPool<TransformComponent>();
        if (!rbPool || !trPool) return;

        for (const auto& [entityID, pos] : m_initialTransforms)
        {
            Entity ent(entityID);
            if (!scene.IsValid(ent)) continue;

            if (auto* tr = trPool->TryGet(ent.GetIndex()))
            {
                tr->Position = pos;
            }
            if (auto* rb = rbPool->TryGet(ent.GetIndex()))
            {
                rb->LinearVelocity = { 0.0f, 0.0f, 0.0f };
                rb->AngularVelocity = { 0.0f, 0.0f, 0.0f };
                rb->IsGrounded = false;
            }
        }
        for (const auto& [entityID, rot] : m_initialRotations)
        {
            Entity ent(entityID);
            if (!scene.IsValid(ent)) continue;

            if (auto* tr = trPool->TryGet(ent.GetIndex()))
            {
                tr->Rotation = rot;
            }
        }
        m_activeTriggerPairs.clear();
        m_triggerEvents.clear();
        m_accumulator = 0.0f;
    }

    void PhysicsSystem::Step(Scene& scene, f32 dt)
    {
        if (dt <= 0.0f) return;

        m_triggerEvents.clear();

        m_accumulator += dt;
        if (m_accumulator > 0.2f)
        {
            m_accumulator = 0.2f;
        }

        while (m_accumulator >= FixedTimeStep)
        {
            Integrate(scene, FixedTimeStep);
            m_accumulator -= FixedTimeStep;
        }
    }

    void PhysicsSystem::Integrate(Scene& scene, f32 dt)
    {
        auto* trPool = scene.GetPool<TransformComponent>();
        if (!trPool) return;

        auto* rbPool = scene.GetPool<RigidBodyComponent>();
        auto* boxPool = scene.GetPool<BoxColliderComponent>();
        auto* spherePool = scene.GetPool<SphereColliderComponent>();

        std::vector<PhysicsBodyEntry> bodies;
        for (const auto& ent : scene.GetAliveEntities())
        {
            const u32 idx = ent.GetIndex();
            auto* tr = trPool->TryGet(idx);
            if (!tr) continue;

            auto* rb = rbPool ? rbPool->TryGet(idx) : nullptr;
            const auto* box = boxPool ? boxPool->TryGet(idx) : nullptr;
            const auto* sphere = spherePool ? spherePool->TryGet(idx) : nullptr;

            if (!rb && !box && !sphere) continue;

            PhysicsBodyEntry b;
            b.Ent = ent;
            b.Transform = tr;
            b.RigidBody = rb;
            b.BoxComp = box;
            b.SphereComp = sphere;
            b.IsSphere = (sphere != nullptr && box == nullptr);
            b.IsTrigger = (box && box->IsTrigger) || (sphere && sphere->IsTrigger);
            b.IsDynamic = (rb != nullptr && rb->Type == BodyType::Dynamic);

            if (b.IsSphere)
            {
                b.SphereShape = BuildSphere(*tr, sphere);
                b.BoundingRadius = b.SphereShape.Radius;
                b.Center = b.SphereShape.Center;
            }
            else
            {
                b.BoxShape = BuildOBB(*tr, box);
                b.BoundingRadius = std::sqrt(
                    b.BoxShape.Extents.x * b.BoxShape.Extents.x +
                    b.BoxShape.Extents.y * b.BoxShape.Extents.y +
                    b.BoxShape.Extents.z * b.BoxShape.Extents.z);
                b.Center = b.BoxShape.Center;
            }

            bodies.push_back(b);
        }

        if (bodies.empty()) return;

        constexpr int kSubSteps = 2;
        const f32 subDt = dt / static_cast<f32>(kSubSteps);
        const usize numBodies = bodies.size();

        std::vector<TriggerPair> sweptTriggerPairs;
        std::vector<TriggerPair> finalActiveTriggerPairs;

        for (int step = 0; step < kSubSteps; ++step)
        {
            std::vector<DirectX::XMFLOAT3> displacements(numBodies, DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f });

            for (usize i = 0; i < numBodies; ++i)
            {
                auto& b = bodies[i];
                if (!b.IsDynamic || !b.RigidBody || !b.Transform) continue;

                auto& rb = *b.RigidBody;
                if (rb.UseGravity)
                {
                    rb.LinearVelocity.y += m_gravity.y * rb.GravityScale * subDt;
                }

                rb.LinearVelocity.x *= std::max(0.0f, 1.0f - rb.LinearDamping * subDt);
                rb.LinearVelocity.y *= std::max(0.0f, 1.0f - rb.LinearDamping * subDt);
                rb.LinearVelocity.z *= std::max(0.0f, 1.0f - rb.LinearDamping * subDt);

                rb.AngularVelocity.x *= std::max(0.0f, 1.0f - rb.AngularDamping * subDt);
                rb.AngularVelocity.y *= std::max(0.0f, 1.0f - rb.AngularDamping * subDt);
                rb.AngularVelocity.z *= std::max(0.0f, 1.0f - rb.AngularDamping * subDt);

                displacements[i] = Mul(rb.LinearVelocity, subDt);
            }

            std::vector<f32> minTOIs(numBodies, 1.0f);
            std::vector<DirectX::XMFLOAT3> hitNormals(numBodies, DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f });
            std::vector<bool> hitGroundFlags(numBodies, false);
            std::vector<bool> hitObstacleFlags(numBodies, false);
            std::vector<usize> hitTargets(numBodies, static_cast<usize>(-1));

            if (m_ccdEnabled)
            {
                for (usize i = 0; i < numBodies; ++i)
                {
                    auto& b1 = bodies[i];
                    if (!b1.IsDynamic || !b1.RigidBody || !b1.Transform || b1.IsTrigger) continue;

                    const DirectX::XMFLOAT3& disp = displacements[i];
                    const f32 dispLenSq = Dot(disp, disp);
                    if (dispLenSq < 1e-8f) continue;

                    f32 bottomY = 0.0f;
                    if (b1.IsSphere)
                    {
                        bottomY = b1.SphereShape.Center.y - b1.SphereShape.Radius;
                    }
                    else
                    {
                        const f32 hy = b1.BoxShape.Extents.x * std::abs(b1.BoxShape.Axes[0].y) +
                                       b1.BoxShape.Extents.y * std::abs(b1.BoxShape.Axes[1].y) +
                                       b1.BoxShape.Extents.z * std::abs(b1.BoxShape.Axes[2].y);
                        bottomY = b1.BoxShape.Center.y - hy;
                    }

                    if (disp.y < -1e-6f && bottomY > 1e-3f)
                    {
                        const f32 t = bottomY / (-disp.y);
                        if (t >= 0.0f && t < minTOIs[i])
                        {
                            minTOIs[i] = t;
                            hitGroundFlags[i] = true;
                            hitObstacleFlags[i] = false;
                            hitNormals[i] = { 0.0f, 1.0f, 0.0f };
                        }
                    }

                    for (usize j = 0; j < numBodies; ++j)
                    {
                        if (j == i) continue;
                        const auto& b2 = bodies[j];

                        const DirectX::XMFLOAT3 relV = Sub(disp, displacements[j]);
                        const f32 relVLen = std::sqrt(Dot(relV, relV));
                        const DirectX::XMFLOAT3 sweptCenter = Add(b1.Center, Mul(relV, 0.5f));
                        const f32 sweptRadius = b1.BoundingRadius + 0.5f * relVLen;
                        const DirectX::XMFLOAT3 deltaC = Sub(b2.Center, sweptCenter);
                        const f32 maxD = sweptRadius + b2.BoundingRadius;

                        if (Dot(deltaC, deltaC) > maxD * maxD) continue;

                        f32 toi = 1.0f;
                        DirectX::XMFLOAT3 norm{ 0.0f, 1.0f, 0.0f };
                        bool hasHit = false;

                        if (b1.IsSphere && b2.IsSphere)
                        {
                            hasHit = SweptSphereSphere(b1.SphereShape.Center, relV, b1.SphereShape.Radius, b2.SphereShape.Center, b2.SphereShape.Radius, toi, norm);
                        }
                        else if (!b1.IsSphere && b2.IsSphere)
                        {
                            hasHit = SweptBoxSphere(b1.BoxShape, relV, b2.SphereShape, toi, norm);
                        }
                        else if (b1.IsSphere && !b2.IsSphere)
                        {
                            hasHit = SweptBoxSphere(b2.BoxShape, Mul(relV, -1.0f), b1.SphereShape, toi, norm);
                            if (hasHit) norm = Mul(norm, -1.0f);
                        }
                        else
                        {
                            hasHit = SweptBoxOBB(b1.BoxShape, relV, b2.BoxShape, toi, norm);
                        }

                        if (hasHit)
                        {
                            if (b2.IsTrigger)
                            {
                                AddTriggerPair(sweptTriggerPairs, b2.Ent, b1.Ent);
                            }
                            else if (toi < minTOIs[i])
                            {
                                minTOIs[i] = toi;
                                hitObstacleFlags[i] = true;
                                hitGroundFlags[i] = false;
                                hitNormals[i] = norm;
                                hitTargets[i] = j;
                            }
                        }
                    }
                }
            }

            for (usize i = 0; i < numBodies; ++i)
            {
                auto& b1 = bodies[i];
                if (!b1.IsDynamic || !b1.RigidBody || !b1.Transform) continue;

                auto& rb = *b1.RigidBody;
                auto* tr = b1.Transform;
                const f32 toi = minTOIs[i];
                const DirectX::XMFLOAT3& disp = displacements[i];

                tr->Position.x += disp.x * toi;
                tr->Position.y += disp.y * toi;
                tr->Position.z += disp.z * toi;

                tr->Rotation.x += rb.AngularVelocity.x * subDt * toi;
                tr->Rotation.y += rb.AngularVelocity.y * subDt * toi;
                tr->Rotation.z += rb.AngularVelocity.z * subDt * toi;

                if (b1.IsSphere)
                {
                    b1.SphereShape = BuildSphere(*tr, b1.SphereComp);
                    b1.Center = b1.SphereShape.Center;
                }
                else
                {
                    b1.BoxShape = BuildOBB(*tr, b1.BoxComp);
                    b1.Center = b1.BoxShape.Center;
                }

                if (toi < 1.0f)
                {
                    if (hitGroundFlags[i])
                    {
                        if (rb.LinearVelocity.y < 0.0f)
                        {
                            rb.LinearVelocity.y = -rb.LinearVelocity.y * rb.Restitution;
                            if (std::abs(rb.LinearVelocity.y) < 0.2f)
                            {
                                rb.LinearVelocity.y = 0.0f;
                                rb.IsGrounded = true;
                            }
                        }
                        rb.LinearVelocity.x *= std::max(0.0f, 1.0f - rb.Friction * subDt * 10.0f);
                        rb.LinearVelocity.z *= std::max(0.0f, 1.0f - rb.Friction * subDt * 10.0f);
                    }
                    else if (hitObstacleFlags[i])
                    {
                        const usize j = hitTargets[i];
                        const auto& norm = hitNormals[i];
                        if (j < numBodies)
                        {
                            auto& b2 = bodies[j];
                            if (b2.IsDynamic && b2.RigidBody)
                            {
                                if (i < j || !hitObstacleFlags[j] || hitTargets[j] != i)
                                {
                                    auto& rb2 = *b2.RigidBody;
                                    const DirectX::XMFLOAT3 vRel = Sub(rb.LinearVelocity, rb2.LinearVelocity);
                                    const f32 vn = Dot(vRel, norm);
                                    if (vn < 0.0f)
                                    {
                                        const f32 e = std::min(rb.Restitution, rb2.Restitution);
                                        const f32 invM1 = 1.0f / std::max(0.001f, rb.Mass);
                                        const f32 invM2 = 1.0f / std::max(0.001f, rb2.Mass);
                                        const f32 impulseMag = -(1.0f + e) * vn / (invM1 + invM2);
                                        rb.LinearVelocity = Add(rb.LinearVelocity, Mul(norm, impulseMag * invM1));
                                        rb2.LinearVelocity = Sub(rb2.LinearVelocity, Mul(norm, impulseMag * invM2));
                                        EventBus::Publish(CollisionEvent{
                                            b1.Ent,
                                            b2.Ent,
                                            norm,
                                            Mul(Add(b1.Center, b2.Center), 0.5f),
                                            impulseMag
                                        });
                                    }
                                }
                            }
                            else
                            {
                                const f32 vn = Dot(rb.LinearVelocity, norm);
                                if (vn < 0.0f)
                                {
                                    const f32 impulseMag = -(1.0f + rb.Restitution) * vn;
                                    rb.LinearVelocity = Sub(rb.LinearVelocity, Mul(norm, (1.0f + rb.Restitution) * vn));
                                    EventBus::Publish(CollisionEvent{
                                        b1.Ent,
                                        b2.Ent,
                                        norm,
                                        Mul(Add(b1.Center, b2.Center), 0.5f),
                                        impulseMag
                                    });
                                }
                            }
                        }
                    }
                }

                if (!b1.IsTrigger)
                {
                    if (b1.IsSphere)
                    {
                        const float bottomY = b1.SphereShape.Center.y - b1.SphereShape.Radius;
                        if (bottomY < 0.0f)
                        {
                            tr->Position.y += -bottomY;
                            b1.SphereShape.Center.y += -bottomY;
                            b1.Center.y += -bottomY;
                            if (rb.LinearVelocity.y < 0.0f)
                            {
                                rb.LinearVelocity.y = -rb.LinearVelocity.y * rb.Restitution;
                                if (std::abs(rb.LinearVelocity.y) < 0.2f)
                                {
                                    rb.LinearVelocity.y = 0.0f;
                                    rb.IsGrounded = true;
                                }
                            }
                            rb.LinearVelocity.x *= std::max(0.0f, 1.0f - rb.Friction * subDt * 10.0f);
                            rb.LinearVelocity.z *= std::max(0.0f, 1.0f - rb.Friction * subDt * 10.0f);
                        }
                        else if (!hitGroundFlags[i])
                        {
                            rb.IsGrounded = false;
                        }
                    }
                    else
                    {
                        DirectX::XMFLOAT3 corners[8];
                        float maxPen = 0.0f;
                        u32 penCount = 0;

                        for (u32 c = 0; c < 8; ++c)
                        {
                            const float sx = (c & 1) ? b1.BoxShape.Extents.x : -b1.BoxShape.Extents.x;
                            const float sy = (c & 2) ? b1.BoxShape.Extents.y : -b1.BoxShape.Extents.y;
                            const float sz = (c & 4) ? b1.BoxShape.Extents.z : -b1.BoxShape.Extents.z;

                            const DirectX::XMFLOAT3 r = Add(Mul(b1.BoxShape.Axes[0], sx), Add(Mul(b1.BoxShape.Axes[1], sy), Mul(b1.BoxShape.Axes[2], sz)));
                            const DirectX::XMFLOAT3 p = Add(b1.BoxShape.Center, r);
                            corners[c] = p;

                            if (p.y < 0.0f)
                            {
                                maxPen = std::max(maxPen, -p.y);
                                penCount++;
                            }
                        }

                        if (penCount > 0)
                        {
                            tr->Position.y += maxPen;
                            b1.BoxShape.Center.y += maxPen;
                            b1.Center.y += maxPen;

                            const float mass = std::max(0.001f, rb.Mass);
                            const float invMass = 1.0f / mass;
                            const DirectX::XMFLOAT3 N{ 0.0f, 1.0f, 0.0f };

                            for (u32 c = 0; c < 8; ++c)
                            {
                                if (corners[c].y < 0.0f)
                                {
                                    const DirectX::XMFLOAT3 r{
                                        corners[c].x - tr->Position.x,
                                        corners[c].y + maxPen - tr->Position.y,
                                        corners[c].z - tr->Position.z
                                    };

                                    const DirectX::XMFLOAT3 angVelTerm = Cross(rb.AngularVelocity, r);
                                    const float vpy = rb.LinearVelocity.y + angVelTerm.y;

                                    if (vpy < 0.0f)
                                    {
                                        const DirectX::XMFLOAT3 tN = Cross(r, N);
                                        const DirectX::XMFLOAT3 invItN = TransformInverseInertia(tN, b1.BoxShape.Axes, b1.BoxShape.Extents, mass);
                                        const float rotInertia = Dot(tN, invItN);
                                        const float invMeff = invMass + rotInertia;

                                        if (invMeff > 1e-6f)
                                        {
                                            const float impulseMag = -(1.0f + rb.Restitution) * vpy / (static_cast<float>(penCount) * invMeff);
                                            rb.LinearVelocity.y += impulseMag * invMass;
                                            rb.AngularVelocity.x += impulseMag * invItN.x;
                                            rb.AngularVelocity.y += impulseMag * invItN.y;
                                            rb.AngularVelocity.z += impulseMag * invItN.z;
                                        }
                                    }
                                }
                            }

                            if (std::abs(rb.LinearVelocity.y) < 0.2f)
                            {
                                rb.LinearVelocity.y = 0.0f;
                                rb.AngularVelocity.x *= 0.8f;
                                rb.AngularVelocity.y *= 0.8f;
                                rb.AngularVelocity.z *= 0.8f;
                                rb.IsGrounded = true;
                            }

                            rb.LinearVelocity.x *= std::max(0.0f, 1.0f - rb.Friction * subDt * 10.0f);
                            rb.LinearVelocity.z *= std::max(0.0f, 1.0f - rb.Friction * subDt * 10.0f);
                            rb.AngularVelocity.x *= std::max(0.0f, 1.0f - rb.Friction * subDt * 10.0f);
                            rb.AngularVelocity.y *= std::max(0.0f, 1.0f - rb.Friction * subDt * 10.0f);
                            rb.AngularVelocity.z *= std::max(0.0f, 1.0f - rb.Friction * subDt * 10.0f);
                        }
                        else if (!hitGroundFlags[i])
                        {
                            rb.IsGrounded = false;
                        }
                    }
                }
            }

            constexpr float kCellSize = 4.0f;
            constexpr float kInvCellSize = 1.0f / kCellSize;

            std::unordered_map<u64, std::vector<usize>> spatialGrid;
            spatialGrid.reserve(numBodies);

            for (usize i = 0; i < numBodies; ++i)
            {
                const auto* tr = bodies[i].Transform;
                if (!tr) continue;

                const i32 cx = static_cast<i32>(std::floor(tr->Position.x * kInvCellSize));
                const i32 cy = static_cast<i32>(std::floor(tr->Position.y * kInvCellSize));
                const i32 cz = static_cast<i32>(std::floor(tr->Position.z * kInvCellSize));
                spatialGrid[HashGridCell(cx, cy, cz)].push_back(i);
            }

            std::vector<TriggerPair> stepActiveTriggers;

            for (usize i = 0; i < numBodies; ++i)
            {
                auto& b1 = bodies[i];
                auto* tr1 = b1.Transform;
                if (!tr1) continue;

                const i32 baseCx = static_cast<i32>(std::floor(tr1->Position.x * kInvCellSize));
                const i32 baseCy = static_cast<i32>(std::floor(tr1->Position.y * kInvCellSize));
                const i32 baseCz = static_cast<i32>(std::floor(tr1->Position.z * kInvCellSize));

                for (i32 dz = -1; dz <= 1; ++dz)
                {
                    for (i32 dy = -1; dy <= 1; ++dy)
                    {
                        for (i32 dx = -1; dx <= 1; ++dx)
                        {
                            const u64 cellKey = HashGridCell(baseCx + dx, baseCy + dy, baseCz + dz);
                            auto it = spatialGrid.find(cellKey);
                            if (it == spatialGrid.end()) continue;

                            for (usize j : it->second)
                            {
                                if (j <= i) continue;

                                auto& b2 = bodies[j];
                                if (!b1.IsDynamic && !b2.IsDynamic) continue;

                                auto* tr2 = b2.Transform;
                                if (!tr2) continue;

                                const DirectX::XMFLOAT3 delta = Sub(b2.Center, b1.Center);
                                const float distSq = Dot(delta, delta);
                                const float minDist = b1.BoundingRadius + b2.BoundingRadius;

                                if (distSq > minDist * minDist) continue;

                                ContactInfo contact;
                                if (!b1.IsSphere && !b2.IsSphere)
                                {
                                    contact = CheckOBBOBB(b1.BoxShape, b2.BoxShape);
                                }
                                else if (!b1.IsSphere && b2.IsSphere)
                                {
                                    contact = CheckBoxSphere(b1.BoxShape, b2.SphereShape);
                                }
                                else if (b1.IsSphere && !b2.IsSphere)
                                {
                                    contact = CheckBoxSphere(b2.BoxShape, b1.SphereShape);
                                    if (contact.HasCollision)
                                    {
                                        contact.Normal = Mul(contact.Normal, -1.0f);
                                    }
                                }
                                else
                                {
                                    contact = CheckSphereSphere(b1.SphereShape, b2.SphereShape);
                                }

                                if (!contact.HasCollision) continue;

                                if (b1.IsTrigger || b2.IsTrigger)
                                {
                                    if (b1.IsTrigger)
                                    {
                                        AddTriggerPair(stepActiveTriggers, b1.Ent, b2.Ent);
                                    }
                                    if (b2.IsTrigger)
                                    {
                                        AddTriggerPair(stepActiveTriggers, b2.Ent, b1.Ent);
                                    }
                                    continue;
                                }

                                auto* rb1 = b1.RigidBody;
                                auto* rb2 = b2.RigidBody;

                                const DirectX::XMFLOAT3 rA = Sub(contact.Point, tr1->Position);
                                const DirectX::XMFLOAT3 rB = Sub(contact.Point, tr2->Position);

                                const DirectX::XMFLOAT3 vPA = (b1.IsDynamic && rb1) ? Add(rb1->LinearVelocity, Cross(rb1->AngularVelocity, rA)) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                                const DirectX::XMFLOAT3 vPB = (b2.IsDynamic && rb2) ? Add(rb2->LinearVelocity, Cross(rb2->AngularVelocity, rB)) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                                const DirectX::XMFLOAT3 vRel = Sub(vPB, vPA);
                                const float velAlongNormal = Dot(vRel, contact.Normal);

                                const float invMass1 = (b1.IsDynamic && rb1) ? (1.0f / std::max(0.001f, rb1->Mass)) : 0.0f;
                                const float invMass2 = (b2.IsDynamic && rb2) ? (1.0f / std::max(0.001f, rb2->Mass)) : 0.0f;
                                const float invMassSum = invMass1 + invMass2;

                                if (invMassSum > 0.0001f)
                                {
                                    const float pen1 = contact.Penetration * (invMass1 / invMassSum);
                                    const float pen2 = contact.Penetration * (invMass2 / invMassSum);

                                    if (b1.IsDynamic && rb1)
                                    {
                                        tr1->Position = Sub(tr1->Position, Mul(contact.Normal, pen1));
                                    }
                                    if (b2.IsDynamic && rb2)
                                    {
                                        tr2->Position = Add(tr2->Position, Mul(contact.Normal, pen2));
                                    }
                                }

                                if (velAlongNormal < 0.0f)
                                {
                                    const float rest1 = rb1 ? rb1->Restitution : 0.5f;
                                    const float rest2 = rb2 ? rb2->Restitution : 0.5f;
                                    const float e = std::min(rest1, rest2);

                                    DirectX::XMFLOAT3 invItN1{ 0.0f, 0.0f, 0.0f };
                                    float rotFactor1 = 0.0f;
                                    if (b1.IsDynamic && rb1)
                                    {
                                        const DirectX::XMFLOAT3 tN1 = Cross(rA, contact.Normal);
                                        if (!b1.IsSphere)
                                        {
                                            invItN1 = TransformInverseInertia(tN1, b1.BoxShape.Axes, b1.BoxShape.Extents, rb1->Mass);
                                        }
                                        else
                                        {
                                            const float Isph = 0.4f * rb1->Mass * b1.SphereShape.Radius * b1.SphereShape.Radius;
                                            const float invI = 1.0f / std::max(1e-4f, Isph);
                                            invItN1 = Mul(tN1, invI);
                                        }
                                        rotFactor1 = Dot(tN1, invItN1);
                                    }

                                    DirectX::XMFLOAT3 invItN2{ 0.0f, 0.0f, 0.0f };
                                    float rotFactor2 = 0.0f;
                                    if (b2.IsDynamic && rb2)
                                    {
                                        const DirectX::XMFLOAT3 tN2 = Cross(rB, contact.Normal);
                                        if (!b2.IsSphere)
                                        {
                                            invItN2 = TransformInverseInertia(tN2, b2.BoxShape.Axes, b2.BoxShape.Extents, rb2->Mass);
                                        }
                                        else
                                        {
                                            const float Isph = 0.4f * rb2->Mass * b2.SphereShape.Radius * b2.SphereShape.Radius;
                                            const float invI = 1.0f / std::max(1e-4f, Isph);
                                            invItN2 = Mul(tN2, invI);
                                        }
                                        rotFactor2 = Dot(tN2, invItN2);
                                    }

                                    const float invMeff = invMass1 + invMass2 + rotFactor1 + rotFactor2;
                                    if (invMeff > 0.0001f)
                                    {
                                        const float impulseMag = -(1.0f + e) * velAlongNormal / invMeff;
                                        const DirectX::XMFLOAT3 impulse = Mul(contact.Normal, impulseMag);

                                        if (b1.IsDynamic && rb1)
                                        {
                                            rb1->LinearVelocity = Sub(rb1->LinearVelocity, Mul(impulse, invMass1));
                                            rb1->AngularVelocity = Sub(rb1->AngularVelocity, Mul(invItN1, impulseMag));
                                        }
                                        if (b2.IsDynamic && rb2)
                                        {
                                            rb2->LinearVelocity = Add(rb2->LinearVelocity, Mul(impulse, invMass2));
                                            rb2->AngularVelocity = Add(rb2->AngularVelocity, Mul(invItN2, impulseMag));
                                        }

                                        EventBus::Publish(CollisionEvent{
                                            b1.Ent,
                                            b2.Ent,
                                            contact.Normal,
                                            contact.Point,
                                            impulseMag
                                        });
                                    }
                                }
                            }
                        }
                    }
                }
            }

            finalActiveTriggerPairs = std::move(stepActiveTriggers);
        }

        std::vector<TriggerEvent> newEvents;
        for (const auto& swept : sweptTriggerPairs)
        {
            auto itPrev = std::find(m_activeTriggerPairs.begin(), m_activeTriggerPairs.end(), swept);
            auto itFinal = std::find(finalActiveTriggerPairs.begin(), finalActiveTriggerPairs.end(), swept);
            if (itPrev == m_activeTriggerPairs.end())
            {
                newEvents.push_back(TriggerEvent{ swept.TriggerEntity, swept.OtherEntity, TriggerEvent::State::Enter });
                if (itFinal == finalActiveTriggerPairs.end())
                {
                    newEvents.push_back(TriggerEvent{ swept.TriggerEntity, swept.OtherEntity, TriggerEvent::State::Exit });
                }
            }
        }

        for (const auto& cur : finalActiveTriggerPairs)
        {
            auto itPrev = std::find(m_activeTriggerPairs.begin(), m_activeTriggerPairs.end(), cur);
            if (itPrev != m_activeTriggerPairs.end())
            {
                newEvents.push_back(TriggerEvent{ cur.TriggerEntity, cur.OtherEntity, TriggerEvent::State::Stay });
            }
            else
            {
                auto itSwept = std::find(sweptTriggerPairs.begin(), sweptTriggerPairs.end(), cur);
                if (itSwept == sweptTriggerPairs.end())
                {
                    newEvents.push_back(TriggerEvent{ cur.TriggerEntity, cur.OtherEntity, TriggerEvent::State::Enter });
                }
            }
        }

        for (const auto& prev : m_activeTriggerPairs)
        {
            auto itFinal = std::find(finalActiveTriggerPairs.begin(), finalActiveTriggerPairs.end(), prev);
            if (itFinal == finalActiveTriggerPairs.end())
            {
                newEvents.push_back(TriggerEvent{ prev.TriggerEntity, prev.OtherEntity, TriggerEvent::State::Exit });
            }
        }

        m_activeTriggerPairs = std::move(finalActiveTriggerPairs);
        for (auto& ev : newEvents)
        {
            EventBus::Publish(PhysicsTriggerEvent{
                ev.TriggerEntity,
                ev.OtherEntity,
                ev.EventState
            });
            m_triggerEvents.push_back(std::move(ev));
        }
    }

    std::optional<RaycastHit> PhysicsSystem::Raycast(
        const Scene& scene,
        const DirectX::XMFLOAT3& origin,
        const DirectX::XMFLOAT3& direction,
        f32 maxDistance)
    {
        const auto* trPool = scene.GetPool<TransformComponent>();
        if (!trPool) return std::nullopt;

        const auto* boxPool = scene.GetPool<BoxColliderComponent>();
        const auto* spherePool = scene.GetPool<SphereColliderComponent>();

        float dirLen = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (dirLen < 1e-6f) return std::nullopt;

        const float dx = direction.x / dirLen;
        const float dy = direction.y / dirLen;
        const float dz = direction.z / dirLen;

        float closestDist = maxDistance;
        std::optional<RaycastHit> hit;

        for (const auto& ent : scene.GetAliveEntities())
        {
            const auto* tr = trPool->TryGet(ent.GetIndex());
            if (!tr) continue;

            const auto* box = boxPool ? boxPool->TryGet(ent.GetIndex()) : nullptr;
            const auto* sphere = spherePool ? spherePool->TryGet(ent.GetIndex()) : nullptr;

            if (sphere && !box)
            {
                const float cx = tr->Position.x + sphere->Offset.x * tr->Scale.x;
                const float cy = tr->Position.y + sphere->Offset.y * tr->Scale.y;
                const float cz = tr->Position.z + sphere->Offset.z * tr->Scale.z;
                const float maxScale = (std::max)({ std::abs(tr->Scale.x), std::abs(tr->Scale.y), std::abs(tr->Scale.z) });
                const float r = (std::max)(sphere->Radius * maxScale, 1e-4f);

                const float ox = origin.x - cx;
                const float oy = origin.y - cy;
                const float oz = origin.z - cz;

                const float b = ox * dx + oy * dy + oz * dz;
                const float c = (ox * ox + oy * oy + oz * oz) - (r * r);

                if (c > 0.0f && b > 0.0f) continue;

                const float discriminant = b * b - c;
                if (discriminant < 0.0f) continue;

                float t = -b - std::sqrt(discriminant);
                if (t < 0.0f) t = 0.0f;

                if (t < closestDist)
                {
                    closestDist = t;
                    RaycastHit h;
                    h.HitEntity = ent;
                    h.Point = { origin.x + dx * t, origin.y + dy * t, origin.z + dz * t };
                    float nx = (h.Point.x - cx) / r;
                    float ny = (h.Point.y - cy) / r;
                    float nz = (h.Point.z - cz) / r;
                    float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (nlen > 1e-4f)
                    {
                        h.Normal = { nx / nlen, ny / nlen, nz / nlen };
                    }
                    else
                    {
                        h.Normal = { -dx, -dy, -dz };
                    }
                    h.Distance = t;
                    hit = h;
                }
            }
            else
            {
                float cx = tr->Position.x;
                float cy = tr->Position.y;
                float cz = tr->Position.z;
                float hx = 0.5f * std::abs(tr->Scale.x);
                float hy = 0.5f * std::abs(tr->Scale.y);
                float hz = 0.5f * std::abs(tr->Scale.z);

                if (box)
                {
                    cx = tr->Position.x + box->Offset.x * tr->Scale.x;
                    cy = tr->Position.y + box->Offset.y * tr->Scale.y;
                    cz = tr->Position.z + box->Offset.z * tr->Scale.z;
                    hx = std::abs(box->HalfSize.x * tr->Scale.x);
                    hy = std::abs(box->HalfSize.y * tr->Scale.y);
                    hz = std::abs(box->HalfSize.z * tr->Scale.z);
                }

                hx = (std::max)(hx, 1e-4f);
                hy = (std::max)(hy, 1e-4f);
                hz = (std::max)(hz, 1e-4f);

                const float minX = cx - hx;
                const float maxX = cx + hx;
                const float minY = cy - hy;
                const float maxY = cy + hy;
                const float minZ = cz - hz;
                const float maxZ = cz + hz;

                float tNear = -1e30f;
                float tFar = 1e30f;
                DirectX::XMFLOAT3 nearNormal{ 0.0f, 1.0f, 0.0f };

                bool missed = false;

                if (std::abs(dx) > 1e-6f)
                {
                    float t1 = (minX - origin.x) / dx;
                    float t2 = (maxX - origin.x) / dx;
                    DirectX::XMFLOAT3 n{ -1.0f, 0.0f, 0.0f };
                    if (t1 > t2)
                    {
                        std::swap(t1, t2);
                        n = { 1.0f, 0.0f, 0.0f };
                    }
                    if (t1 > tNear)
                    {
                        tNear = t1;
                        nearNormal = n;
                    }
                    if (t2 < tFar)
                    {
                        tFar = t2;
                    }
                }
                else if (origin.x < minX || origin.x > maxX)
                {
                    missed = true;
                }

                if (!missed)
                {
                    if (std::abs(dy) > 1e-6f)
                    {
                        float t1 = (minY - origin.y) / dy;
                        float t2 = (maxY - origin.y) / dy;
                        DirectX::XMFLOAT3 n{ 0.0f, -1.0f, 0.0f };
                        if (t1 > t2)
                        {
                            std::swap(t1, t2);
                            n = { 0.0f, 1.0f, 0.0f };
                        }
                        if (t1 > tNear)
                        {
                            tNear = t1;
                            nearNormal = n;
                        }
                        if (t2 < tFar)
                        {
                            tFar = t2;
                        }
                    }
                    else if (origin.y < minY || origin.y > maxY)
                    {
                        missed = true;
                    }
                }

                if (!missed)
                {
                    if (std::abs(dz) > 1e-6f)
                    {
                        float t1 = (minZ - origin.z) / dz;
                        float t2 = (maxZ - origin.z) / dz;
                        DirectX::XMFLOAT3 n{ 0.0f, 0.0f, -1.0f };
                        if (t1 > t2)
                        {
                            std::swap(t1, t2);
                            n = { 0.0f, 0.0f, 1.0f };
                        }
                        if (t1 > tNear)
                        {
                            tNear = t1;
                            nearNormal = n;
                        }
                        if (t2 < tFar)
                        {
                            tFar = t2;
                        }
                    }
                    else if (origin.z < minZ || origin.z > maxZ)
                    {
                        missed = true;
                    }
                }

                if (missed) continue;

                if (tNear <= tFar && tFar >= 0.0f && tNear < closestDist)
                {
                    const float t = (tNear >= 0.0f) ? tNear : 0.0f;
                    closestDist = t;
                    RaycastHit h;
                    h.HitEntity = ent;
                    h.Point = { origin.x + dx * t, origin.y + dy * t, origin.z + dz * t };
                    h.Normal = nearNormal;
                    h.Distance = t;
                    hit = h;
                }
            }
        }

        return hit;
    }
}
