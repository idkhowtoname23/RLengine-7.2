#include "Scene/Systems/PhysicsSystem.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/Events/CommonEvents.hpp"
#include "Core/Logger.hpp"

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

        inline f32 LengthSq(const DirectX::XMFLOAT3& a) noexcept
        {
            return Dot(a, a);
        }

        inline f32 Length(const DirectX::XMFLOAT3& a) noexcept
        {
            return std::sqrt(Dot(a, a));
        }

        inline DirectX::XMFLOAT3 Normalize(const DirectX::XMFLOAT3& a) noexcept
        {
            const f32 len = Length(a);
            if (len > 1e-6f)
            {
                return Mul(a, 1.0f / len);
            }
            return { 0.0f, 1.0f, 0.0f };
        }

        inline DirectX::XMFLOAT3 RotateVector(const DirectX::XMFLOAT3& v, const DirectX::XMFLOAT3& rotEuler) noexcept
        {
            const auto R = DirectX::XMMatrixRotationRollPitchYaw(rotEuler.x, rotEuler.y, rotEuler.z);
            const auto vec = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(v.x, v.y, v.z, 0.0f), R);
            return { DirectX::XMVectorGetX(vec), DirectX::XMVectorGetY(vec), DirectX::XMVectorGetZ(vec) };
        }

        inline DirectX::XMFLOAT3 GetWorldPoint(const TransformComponent& tr, const DirectX::XMFLOAT3& localPoint) noexcept
        {
            const auto mat = tr.GetWorldMatrix();
            const auto pt = DirectX::XMVector3Transform(DirectX::XMVectorSet(localPoint.x, localPoint.y, localPoint.z, 1.0f), mat);
            return { DirectX::XMVectorGetX(pt), DirectX::XMVectorGetY(pt), DirectX::XMVectorGetZ(pt) };
        }

        inline DirectX::XMFLOAT3 GetWorldDirection(const TransformComponent& tr, const DirectX::XMFLOAT3& localDir) noexcept
        {
            const auto R = DirectX::XMMatrixRotationRollPitchYaw(tr.Rotation.x, tr.Rotation.y, tr.Rotation.z);
            const auto dir = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(localDir.x, localDir.y, localDir.z, 0.0f), R);
            return Normalize({ DirectX::XMVectorGetX(dir), DirectX::XMVectorGetY(dir), DirectX::XMVectorGetZ(dir) });
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

        struct Capsule
        {
            DirectX::XMFLOAT3 A{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 B{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Center{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Axis{ 0.0f, 1.0f, 0.0f };
            f32 Radius = 0.4f;
            f32 HalfCylinder = 0.5f;
        };

        enum class ColliderShapeType : u8
        {
            Box = 0,
            Sphere,
            Capsule
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
            const CapsuleColliderComponent* CapsuleComp = nullptr;
            ColliderShapeType ShapeType = ColliderShapeType::Box;
            bool IsTrigger = false;
            bool IsDynamic = false;
            OBB BoxShape{};
            Sphere SphereShape{};
            Capsule CapsuleShape{};
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

        inline Capsule BuildCapsule(const TransformComponent& tr, const CapsuleColliderComponent* cap) noexcept
        {
            Capsule c;
            const f32 baseRadius = cap ? cap->Radius : 0.4f;
            const f32 baseHeight = cap ? cap->Height : 1.8f;
            const DirectX::XMFLOAT3 offset = cap ? cap->Offset : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };

            const f32 radialScale = std::max(std::abs(tr.Scale.x), std::abs(tr.Scale.z));
            c.Radius = std::max(1e-4f, baseRadius * radialScale);
            const f32 scaledHeight = std::abs(baseHeight * tr.Scale.y);
            c.HalfCylinder = std::max(0.0f, 0.5f * scaledHeight - c.Radius);

            const auto rot = DirectX::XMMatrixRotationRollPitchYaw(tr.Rotation.x, tr.Rotation.y, tr.Rotation.z);
            const DirectX::XMFLOAT3 axisX{ DirectX::XMVectorGetX(rot.r[0]), DirectX::XMVectorGetY(rot.r[0]), DirectX::XMVectorGetZ(rot.r[0]) };
            const DirectX::XMFLOAT3 axisY{ DirectX::XMVectorGetX(rot.r[1]), DirectX::XMVectorGetY(rot.r[1]), DirectX::XMVectorGetZ(rot.r[1]) };
            const DirectX::XMFLOAT3 axisZ{ DirectX::XMVectorGetX(rot.r[2]), DirectX::XMVectorGetY(rot.r[2]), DirectX::XMVectorGetZ(rot.r[2]) };
            c.Axis = Normalize(axisY);

            const DirectX::XMFLOAT3 scaledOffset{
                offset.x * tr.Scale.x,
                offset.y * tr.Scale.y,
                offset.z * tr.Scale.z
            };

            c.Center = {
                tr.Position.x + axisX.x * scaledOffset.x + axisY.x * scaledOffset.y + axisZ.x * scaledOffset.z,
                tr.Position.y + axisX.y * scaledOffset.x + axisY.y * scaledOffset.y + axisZ.y * scaledOffset.z,
                tr.Position.z + axisX.z * scaledOffset.x + axisY.z * scaledOffset.y + axisZ.z * scaledOffset.z
            };

            c.A = Sub(c.Center, Mul(c.Axis, c.HalfCylinder));
            c.B = Add(c.Center, Mul(c.Axis, c.HalfCylinder));
            return c;
        }

        inline DirectX::XMFLOAT3 ClosestPointOnSegment(
            const DirectX::XMFLOAT3& p,
            const DirectX::XMFLOAT3& a,
            const DirectX::XMFLOAT3& b) noexcept
        {
            const DirectX::XMFLOAT3 ab = Sub(b, a);
            const f32 lenSq = Dot(ab, ab);
            if (lenSq < 1e-8f)
            {
                return a;
            }
            const f32 t = std::clamp(Dot(Sub(p, a), ab) / lenSq, 0.0f, 1.0f);
            return Add(a, Mul(ab, t));
        }

        inline void ClosestPointsBetweenSegments(
            const DirectX::XMFLOAT3& a1,
            const DirectX::XMFLOAT3& b1,
            const DirectX::XMFLOAT3& a2,
            const DirectX::XMFLOAT3& b2,
            DirectX::XMFLOAT3& outP1,
            DirectX::XMFLOAT3& outP2) noexcept
        {
            const DirectX::XMFLOAT3 d1 = Sub(b1, a1);
            const DirectX::XMFLOAT3 d2 = Sub(b2, a2);
            const DirectX::XMFLOAT3 r = Sub(a1, a2);
            const f32 a = Dot(d1, d1);
            const f32 e = Dot(d2, d2);
            const f32 f = Dot(d2, r);

            f32 s = 0.0f;
            f32 t = 0.0f;

            if (a <= 1e-8f && e <= 1e-8f)
            {
                s = 0.0f;
                t = 0.0f;
            }
            else if (a <= 1e-8f)
            {
                s = 0.0f;
                t = std::clamp(f / e, 0.0f, 1.0f);
            }
            else
            {
                const f32 c = Dot(d1, r);
                if (e <= 1e-8f)
                {
                    t = 0.0f;
                    s = std::clamp(-c / a, 0.0f, 1.0f);
                }
                else
                {
                    const f32 b = Dot(d1, d2);
                    const f32 denom = a * e - b * b;
                    s = (denom != 0.0f) ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
                    t = (b * s + f) / e;
                    if (t < 0.0f)
                    {
                        t = 0.0f;
                        s = std::clamp(-c / a, 0.0f, 1.0f);
                    }
                    else if (t > 1.0f)
                    {
                        t = 1.0f;
                        s = std::clamp((b - c) / a, 0.0f, 1.0f);
                    }
                }
            }

            outP1 = Add(a1, Mul(d1, s));
            outP2 = Add(a2, Mul(d2, t));
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

        inline DirectX::XMFLOAT3 GetInverseInertiaTensor(
            const DirectX::XMFLOAT3& torque,
            const TransformComponent& tr,
            const BoxColliderComponent* box,
            const SphereColliderComponent* sphere,
            const CapsuleColliderComponent* capsule,
            f32 mass) noexcept
        {
            const f32 m = std::max(0.001f, mass);
            if (sphere && !box)
            {
                const f32 maxScale = std::max({ std::abs(tr.Scale.x), std::abs(tr.Scale.y), std::abs(tr.Scale.z) });
                const f32 r = std::max(1e-4f, sphere->Radius * maxScale);
                const f32 Isph = 0.4f * m * r * r;
                const f32 invI = 1.0f / std::max(1e-4f, Isph);
                return Mul(torque, invI);
            }
            if (capsule && !box && !sphere)
            {
                const Capsule c = BuildCapsule(tr, capsule);
                const auto rot = DirectX::XMMatrixRotationRollPitchYaw(tr.Rotation.x, tr.Rotation.y, tr.Rotation.z);
                const DirectX::XMFLOAT3 axes[3]{
                    { DirectX::XMVectorGetX(rot.r[0]), DirectX::XMVectorGetY(rot.r[0]), DirectX::XMVectorGetZ(rot.r[0]) },
                    { DirectX::XMVectorGetX(rot.r[1]), DirectX::XMVectorGetY(rot.r[1]), DirectX::XMVectorGetZ(rot.r[1]) },
                    { DirectX::XMVectorGetX(rot.r[2]), DirectX::XMVectorGetY(rot.r[2]), DirectX::XMVectorGetZ(rot.r[2]) }
                };
                const DirectX::XMFLOAT3 extents{ c.Radius, c.HalfCylinder + c.Radius, c.Radius };
                return TransformInverseInertia(torque, axes, extents, m);
            }
            OBB obb = BuildOBB(tr, box);
            return TransformInverseInertia(torque, obb.Axes, obb.Extents, m);
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

        inline ContactInfo CheckCapsuleSphere(const Capsule& a, const Sphere& b) noexcept
        {
            const DirectX::XMFLOAT3 pA = ClosestPointOnSegment(b.Center, a.A, a.B);
            const DirectX::XMFLOAT3 D = Sub(b.Center, pA);
            const f32 distSq = Dot(D, D);
            const f32 minDist = a.Radius + b.Radius;

            if (distSq < minDist * minDist)
            {
                ContactInfo info;
                info.HasCollision = true;
                if (distSq > 1e-8f)
                {
                    const f32 dist = std::sqrt(distSq);
                    info.Normal = Mul(D, 1.0f / dist);
                    info.Penetration = minDist - dist;
                }
                else
                {
                    info.Normal = { 0.0f, 1.0f, 0.0f };
                    info.Penetration = minDist;
                }
                info.Point = Add(pA, Mul(info.Normal, a.Radius));
                return info;
            }

            return {};
        }

        inline ContactInfo CheckCapsuleCapsule(const Capsule& a, const Capsule& b) noexcept
        {
            DirectX::XMFLOAT3 pA{};
            DirectX::XMFLOAT3 pB{};
            ClosestPointsBetweenSegments(a.A, a.B, b.A, b.B, pA, pB);

            const DirectX::XMFLOAT3 D = Sub(pB, pA);
            const f32 distSq = Dot(D, D);
            const f32 minDist = a.Radius + b.Radius;

            if (distSq < minDist * minDist)
            {
                ContactInfo info;
                info.HasCollision = true;
                if (distSq > 1e-8f)
                {
                    const f32 dist = std::sqrt(distSq);
                    info.Normal = Mul(D, 1.0f / dist);
                    info.Penetration = minDist - dist;
                }
                else
                {
                    DirectX::XMFLOAT3 fallback = Cross(a.Axis, b.Axis);
                    if (LengthSq(fallback) < 1e-6f)
                    {
                        const DirectX::XMFLOAT3 ref = (std::abs(a.Axis.y) < 0.9f) ? DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f } : DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f };
                        fallback = Cross(a.Axis, ref);
                    }
                    info.Normal = Normalize(fallback);
                    info.Penetration = minDist;
                }
                info.Point = Add(pA, Mul(info.Normal, a.Radius));
                return info;
            }

            return {};
        }

        inline ContactInfo CheckBoxCapsule(const OBB& a, const Capsule& b) noexcept
        {
            auto lerpSeg = [&](f32 t) noexcept -> DirectX::XMFLOAT3
            {
                return Add(b.A, Mul(Sub(b.B, b.A), t));
            };

            auto distSqToOBB = [&](const DirectX::XMFLOAT3& p) noexcept -> f32
            {
                const DirectX::XMFLOAT3 D = Sub(p, a.Center);
                const f32 d0 = Dot(D, a.Axes[0]);
                const f32 d1 = Dot(D, a.Axes[1]);
                const f32 d2 = Dot(D, a.Axes[2]);
                const f32 ex0 = d0 - std::clamp(d0, -a.Extents.x, a.Extents.x);
                const f32 ex1 = d1 - std::clamp(d1, -a.Extents.y, a.Extents.y);
                const f32 ex2 = d2 - std::clamp(d2, -a.Extents.z, a.Extents.z);
                return ex0 * ex0 + ex1 * ex1 + ex2 * ex2;
            };

            constexpr int kSamples = 16;
            f32 bestT = 0.0f;
            f32 bestDistSq = distSqToOBB(b.A);
            for (int s = 1; s <= kSamples; ++s)
            {
                const f32 t = static_cast<f32>(s) / static_cast<f32>(kSamples);
                const f32 dSq = distSqToOBB(lerpSeg(t));
                if (dSq < bestDistSq)
                {
                    bestDistSq = dSq;
                    bestT = t;
                }
            }

            f32 lo = std::max(0.0f, bestT - 1.0f / static_cast<f32>(kSamples));
            f32 hi = std::min(1.0f, bestT + 1.0f / static_cast<f32>(kSamples));
            constexpr f32 kInvPhi = 0.61803398875f;
            f32 x1 = hi - kInvPhi * (hi - lo);
            f32 x2 = lo + kInvPhi * (hi - lo);
            f32 f1 = distSqToOBB(lerpSeg(x1));
            f32 f2 = distSqToOBB(lerpSeg(x2));

            for (int iter = 0; iter < 12; ++iter)
            {
                if (f1 < f2)
                {
                    hi = x2;
                    x2 = x1;
                    f2 = f1;
                    x1 = hi - kInvPhi * (hi - lo);
                    f1 = distSqToOBB(lerpSeg(x1));
                }
                else
                {
                    lo = x1;
                    x1 = x2;
                    f1 = f2;
                    x2 = lo + kInvPhi * (hi - lo);
                    f2 = distSqToOBB(lerpSeg(x2));
                }
            }

            const f32 refinedT = 0.5f * (lo + hi);
            const DirectX::XMFLOAT3 refinedPt = lerpSeg(refinedT);
            if (distSqToOBB(refinedPt) <= 1e-8f)
            {
                ContactInfo deepest{};
                constexpr f32 interiorTs[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
                for (f32 t : interiorTs)
                {
                    const ContactInfo c = CheckBoxSphere(a, Sphere{ lerpSeg(t), b.Radius });
                    if (c.HasCollision && (!deepest.HasCollision || c.Penetration > deepest.Penetration))
                    {
                        deepest = c;
                    }
                }
                return deepest;
            }

            return CheckBoxSphere(a, Sphere{ refinedPt, b.Radius });
        }

        inline bool SweptBodyPair(
            const PhysicsBodyEntry& b1,
            const PhysicsBodyEntry& b2,
            const DirectX::XMFLOAT3& relV,
            f32& outTOI,
            DirectX::XMFLOAT3& outNormal) noexcept
        {
            if (b1.ShapeType == ColliderShapeType::Sphere && b2.ShapeType == ColliderShapeType::Sphere)
            {
                return SweptSphereSphere(b1.SphereShape.Center, relV, b1.SphereShape.Radius, b2.SphereShape.Center, b2.SphereShape.Radius, outTOI, outNormal);
            }
            if (b1.ShapeType == ColliderShapeType::Box && b2.ShapeType == ColliderShapeType::Sphere)
            {
                return SweptBoxSphere(b1.BoxShape, relV, b2.SphereShape, outTOI, outNormal);
            }
            if (b1.ShapeType == ColliderShapeType::Sphere && b2.ShapeType == ColliderShapeType::Box)
            {
                const bool hit = SweptBoxSphere(b2.BoxShape, Mul(relV, -1.0f), b1.SphereShape, outTOI, outNormal);
                if (hit) outNormal = Mul(outNormal, -1.0f);
                return hit;
            }
            if (b1.ShapeType == ColliderShapeType::Box && b2.ShapeType == ColliderShapeType::Box)
            {
                return SweptBoxOBB(b1.BoxShape, relV, b2.BoxShape, outTOI, outNormal);
            }

            f32 bestTOI = 1.0f;
            DirectX::XMFLOAT3 bestNormal{ 0.0f, 1.0f, 0.0f };
            bool anyHit = false;

            auto tryUpdate = [&](bool hit, f32 toi, const DirectX::XMFLOAT3& n) noexcept
            {
                if (hit && toi < bestTOI)
                {
                    bestTOI = toi;
                    bestNormal = n;
                    anyHit = true;
                }
            };

            if (b1.ShapeType == ColliderShapeType::Capsule && b2.ShapeType == ColliderShapeType::Sphere)
            {
                const DirectX::XMFLOAT3 closest = ClosestPointOnSegment(b2.SphereShape.Center, b1.CapsuleShape.A, b1.CapsuleShape.B);
                const DirectX::XMFLOAT3 pts[4] = { b1.CapsuleShape.A, b1.CapsuleShape.Center, b1.CapsuleShape.B, closest };
                for (const auto& pt : pts)
                {
                    f32 toi = 1.0f;
                    DirectX::XMFLOAT3 norm{};
                    tryUpdate(SweptSphereSphere(pt, relV, b1.CapsuleShape.Radius, b2.SphereShape.Center, b2.SphereShape.Radius, toi, norm), toi, norm);
                }
            }
            else if (b1.ShapeType == ColliderShapeType::Sphere && b2.ShapeType == ColliderShapeType::Capsule)
            {
                const DirectX::XMFLOAT3 closest = ClosestPointOnSegment(b1.SphereShape.Center, b2.CapsuleShape.A, b2.CapsuleShape.B);
                const DirectX::XMFLOAT3 pts[4] = { b2.CapsuleShape.A, b2.CapsuleShape.Center, b2.CapsuleShape.B, closest };
                for (const auto& pt : pts)
                {
                    f32 toi = 1.0f;
                    DirectX::XMFLOAT3 norm{};
                    tryUpdate(SweptSphereSphere(b1.SphereShape.Center, relV, b1.SphereShape.Radius, pt, b2.CapsuleShape.Radius, toi, norm), toi, norm);
                }
            }
            else if (b1.ShapeType == ColliderShapeType::Capsule && b2.ShapeType == ColliderShapeType::Box)
            {
                const DirectX::XMFLOAT3 pts[3] = { b1.CapsuleShape.A, b1.CapsuleShape.Center, b1.CapsuleShape.B };
                const DirectX::XMFLOAT3 negV = Mul(relV, -1.0f);
                for (const auto& pt : pts)
                {
                    f32 toi = 1.0f;
                    DirectX::XMFLOAT3 norm{};
                    if (SweptBoxSphere(b2.BoxShape, negV, Sphere{ pt, b1.CapsuleShape.Radius }, toi, norm))
                    {
                        tryUpdate(true, toi, Mul(norm, -1.0f));
                    }
                }
            }
            else if (b1.ShapeType == ColliderShapeType::Box && b2.ShapeType == ColliderShapeType::Capsule)
            {
                const DirectX::XMFLOAT3 pts[3] = { b2.CapsuleShape.A, b2.CapsuleShape.Center, b2.CapsuleShape.B };
                for (const auto& pt : pts)
                {
                    f32 toi = 1.0f;
                    DirectX::XMFLOAT3 norm{};
                    tryUpdate(SweptBoxSphere(b1.BoxShape, relV, Sphere{ pt, b2.CapsuleShape.Radius }, toi, norm), toi, norm);
                }
            }
            else
            {
                DirectX::XMFLOAT3 p1{};
                DirectX::XMFLOAT3 p2{};
                ClosestPointsBetweenSegments(b1.CapsuleShape.A, b1.CapsuleShape.B, b2.CapsuleShape.A, b2.CapsuleShape.B, p1, p2);
                const DirectX::XMFLOAT3 pts1[4] = { p1, b1.CapsuleShape.A, b1.CapsuleShape.Center, b1.CapsuleShape.B };
                const DirectX::XMFLOAT3 pts2[4] = { p2, b2.CapsuleShape.A, b2.CapsuleShape.Center, b2.CapsuleShape.B };
                for (int k = 0; k < 4; ++k)
                {
                    f32 toi = 1.0f;
                    DirectX::XMFLOAT3 norm{};
                    tryUpdate(SweptSphereSphere(pts1[k], relV, b1.CapsuleShape.Radius, pts2[k], b2.CapsuleShape.Radius, toi, norm), toi, norm);
                }
            }

            if (anyHit)
            {
                outTOI = bestTOI;
                outNormal = bestNormal;
            }
            return anyHit;
        }
    }

    void PhysicsSystem::ClearSnapshot() noexcept
    {
        m_initialTransforms.clear();
        m_initialRotations.clear();
        m_hingeStates.clear();
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
        m_hingeStates.clear();
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
        auto* capsulePool = scene.GetPool<CapsuleColliderComponent>();

        std::vector<PhysicsBodyEntry> bodies;
        for (const auto& ent : scene.GetAliveEntities())
        {
            const u32 idx = ent.GetIndex();
            auto* tr = trPool->TryGet(idx);
            if (!tr) continue;

            auto* rb = rbPool ? rbPool->TryGet(idx) : nullptr;
            const auto* box = boxPool ? boxPool->TryGet(idx) : nullptr;
            const auto* sphere = spherePool ? spherePool->TryGet(idx) : nullptr;
            const auto* capsule = capsulePool ? capsulePool->TryGet(idx) : nullptr;

            if (!rb && !box && !sphere && !capsule) continue;

            PhysicsBodyEntry b;
            b.Ent = ent;
            b.Transform = tr;
            b.RigidBody = rb;
            b.BoxComp = box;
            b.SphereComp = sphere;
            b.CapsuleComp = capsule;
            if (sphere && !box)
            {
                b.ShapeType = ColliderShapeType::Sphere;
            }
            else if (capsule && !box && !sphere)
            {
                b.ShapeType = ColliderShapeType::Capsule;
            }
            else
            {
                b.ShapeType = ColliderShapeType::Box;
            }
            b.IsTrigger = (box && box->IsTrigger) || (sphere && sphere->IsTrigger) || (capsule && capsule->IsTrigger);
            b.IsDynamic = (rb != nullptr && rb->Type == BodyType::Dynamic);

            if (b.ShapeType == ColliderShapeType::Sphere)
            {
                b.SphereShape = BuildSphere(*tr, sphere);
                b.BoundingRadius = b.SphereShape.Radius;
                b.Center = b.SphereShape.Center;
            }
            else if (b.ShapeType == ColliderShapeType::Capsule)
            {
                b.CapsuleShape = BuildCapsule(*tr, capsule);
                b.BoundingRadius = b.CapsuleShape.HalfCylinder + b.CapsuleShape.Radius;
                b.Center = b.CapsuleShape.Center;
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

        if (bodies.empty())
        {
            SolveConstraints(scene, dt);
            return;
        }

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
                    f32 minDispY = disp.y;
                    if (b1.ShapeType == ColliderShapeType::Sphere)
                    {
                        bottomY = b1.SphereShape.Center.y - b1.SphereShape.Radius;
                    }
                    else if (b1.ShapeType == ColliderShapeType::Capsule)
                    {
                        const DirectX::XMFLOAT3 rA = Sub(b1.CapsuleShape.A, b1.Transform->Position);
                        const DirectX::XMFLOAT3 rB = Sub(b1.CapsuleShape.B, b1.Transform->Position);
                        const f32 dispAY = disp.y + Cross(b1.RigidBody->AngularVelocity, rA).y * subDt;
                        const f32 dispBY = disp.y + Cross(b1.RigidBody->AngularVelocity, rB).y * subDt;
                        const f32 botA = b1.CapsuleShape.A.y - b1.CapsuleShape.Radius;
                        const f32 botB = b1.CapsuleShape.B.y - b1.CapsuleShape.Radius;
                        if (botA <= botB)
                        {
                            bottomY = botA;
                            minDispY = std::min(disp.y, dispAY);
                        }
                        else
                        {
                            bottomY = botB;
                            minDispY = std::min(disp.y, dispBY);
                        }
                    }
                    else
                    {
                        const f32 hy = b1.BoxShape.Extents.x * std::abs(b1.BoxShape.Axes[0].y) +
                                       b1.BoxShape.Extents.y * std::abs(b1.BoxShape.Axes[1].y) +
                                       b1.BoxShape.Extents.z * std::abs(b1.BoxShape.Axes[2].y);
                        bottomY = b1.BoxShape.Center.y - hy;
                    }

                    if (minDispY < -1e-6f && bottomY > 1e-3f && (bottomY + minDispY) < 0.0f)
                    {
                        const f32 t = std::clamp(bottomY / (-minDispY), 0.0f, 1.0f);
                        if (t < minTOIs[i])
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
                        const bool hasHit = SweptBodyPair(b1, b2, relV, toi, norm);

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

                if (b1.ShapeType == ColliderShapeType::Sphere)
                {
                    b1.SphereShape = BuildSphere(*tr, b1.SphereComp);
                    b1.Center = b1.SphereShape.Center;
                }
                else if (b1.ShapeType == ColliderShapeType::Capsule)
                {
                    b1.CapsuleShape = BuildCapsule(*tr, b1.CapsuleComp);
                    b1.Center = b1.CapsuleShape.Center;
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
                    if (b1.ShapeType == ColliderShapeType::Sphere)
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
                    else if (b1.ShapeType == ColliderShapeType::Capsule)
                    {
                        const DirectX::XMFLOAT3 endPoints[2] = {
                            { b1.CapsuleShape.A.x, b1.CapsuleShape.A.y - b1.CapsuleShape.Radius, b1.CapsuleShape.A.z },
                            { b1.CapsuleShape.B.x, b1.CapsuleShape.B.y - b1.CapsuleShape.Radius, b1.CapsuleShape.B.z }
                        };
                        float maxPen = 0.0f;
                        u32 penCount = 0;
                        for (u32 c = 0; c < 2; ++c)
                        {
                            if (endPoints[c].y < 0.0f)
                            {
                                maxPen = std::max(maxPen, -endPoints[c].y);
                                penCount++;
                            }
                        }

                        if (penCount > 0)
                        {
                            tr->Position.y += maxPen;
                            b1.CapsuleShape.A.y += maxPen;
                            b1.CapsuleShape.B.y += maxPen;
                            b1.CapsuleShape.Center.y += maxPen;
                            b1.Center.y += maxPen;

                            const float mass = std::max(0.001f, rb.Mass);
                            const float invMass = 1.0f / mass;
                            const DirectX::XMFLOAT3 N{ 0.0f, 1.0f, 0.0f };

                            for (u32 c = 0; c < 2; ++c)
                            {
                                if (endPoints[c].y < 0.0f)
                                {
                                    const DirectX::XMFLOAT3 r{
                                        endPoints[c].x - tr->Position.x,
                                        endPoints[c].y + maxPen - tr->Position.y,
                                        endPoints[c].z - tr->Position.z
                                    };
                                    const DirectX::XMFLOAT3 angVelTerm = Cross(rb.AngularVelocity, r);
                                    const float vpy = rb.LinearVelocity.y + angVelTerm.y;

                                    if (vpy < 0.0f)
                                    {
                                        const DirectX::XMFLOAT3 tN = Cross(r, N);
                                        const DirectX::XMFLOAT3 invItN = GetInverseInertiaTensor(tN, *tr, b1.BoxComp, b1.SphereComp, b1.CapsuleComp, mass);
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
                                if (b1.ShapeType == ColliderShapeType::Box && b2.ShapeType == ColliderShapeType::Box)
                                {
                                    contact = CheckOBBOBB(b1.BoxShape, b2.BoxShape);
                                }
                                else if (b1.ShapeType == ColliderShapeType::Box && b2.ShapeType == ColliderShapeType::Sphere)
                                {
                                    contact = CheckBoxSphere(b1.BoxShape, b2.SphereShape);
                                }
                                else if (b1.ShapeType == ColliderShapeType::Sphere && b2.ShapeType == ColliderShapeType::Box)
                                {
                                    contact = CheckBoxSphere(b2.BoxShape, b1.SphereShape);
                                    if (contact.HasCollision)
                                    {
                                        contact.Normal = Mul(contact.Normal, -1.0f);
                                    }
                                }
                                else if (b1.ShapeType == ColliderShapeType::Sphere && b2.ShapeType == ColliderShapeType::Sphere)
                                {
                                    contact = CheckSphereSphere(b1.SphereShape, b2.SphereShape);
                                }
                                else if (b1.ShapeType == ColliderShapeType::Capsule && b2.ShapeType == ColliderShapeType::Capsule)
                                {
                                    contact = CheckCapsuleCapsule(b1.CapsuleShape, b2.CapsuleShape);
                                }
                                else if (b1.ShapeType == ColliderShapeType::Capsule && b2.ShapeType == ColliderShapeType::Sphere)
                                {
                                    contact = CheckCapsuleSphere(b1.CapsuleShape, b2.SphereShape);
                                }
                                else if (b1.ShapeType == ColliderShapeType::Sphere && b2.ShapeType == ColliderShapeType::Capsule)
                                {
                                    contact = CheckCapsuleSphere(b2.CapsuleShape, b1.SphereShape);
                                    if (contact.HasCollision)
                                    {
                                        contact.Normal = Mul(contact.Normal, -1.0f);
                                    }
                                }
                                else if (b1.ShapeType == ColliderShapeType::Box && b2.ShapeType == ColliderShapeType::Capsule)
                                {
                                    contact = CheckBoxCapsule(b1.BoxShape, b2.CapsuleShape);
                                }
                                else if (b1.ShapeType == ColliderShapeType::Capsule && b2.ShapeType == ColliderShapeType::Box)
                                {
                                    contact = CheckBoxCapsule(b2.BoxShape, b1.CapsuleShape);
                                    if (contact.HasCollision)
                                    {
                                        contact.Normal = Mul(contact.Normal, -1.0f);
                                    }
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
                                        invItN1 = GetInverseInertiaTensor(tN1, *tr1, b1.BoxComp, b1.SphereComp, b1.CapsuleComp, rb1->Mass);
                                        rotFactor1 = Dot(tN1, invItN1);
                                    }

                                    DirectX::XMFLOAT3 invItN2{ 0.0f, 0.0f, 0.0f };
                                    float rotFactor2 = 0.0f;
                                    if (b2.IsDynamic && rb2)
                                    {
                                        const DirectX::XMFLOAT3 tN2 = Cross(rB, contact.Normal);
                                        invItN2 = GetInverseInertiaTensor(tN2, *tr2, b2.BoxComp, b2.SphereComp, b2.CapsuleComp, rb2->Mass);
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
            SolveConstraints(scene, subDt);

            constexpr f32 kSleepSpeedThreshold = 0.03f;
            constexpr f32 kSleepSpeedThresholdSq = kSleepSpeedThreshold * kSleepSpeedThreshold;
            constexpr f32 kSnapZeroThresholdSq = 0.005f * 0.005f;
            const f32 sleepDamp = std::max(0.0f, 1.0f - 25.0f * subDt);

            for (usize i = 0; i < numBodies; ++i)
            {
                auto& b = bodies[i];
                if (!b.IsDynamic || !b.RigidBody) continue;
                auto& rb = *b.RigidBody;
                if (!rb.IsGrounded) continue;

                const f32 linSq = Dot(rb.LinearVelocity, rb.LinearVelocity);
                if (linSq < kSleepSpeedThresholdSq)
                {
                    if (linSq < kSnapZeroThresholdSq)
                    {
                        rb.LinearVelocity = { 0.0f, 0.0f, 0.0f };
                    }
                    else
                    {
                        rb.LinearVelocity = Mul(rb.LinearVelocity, sleepDamp);
                    }
                }

                const f32 angSq = Dot(rb.AngularVelocity, rb.AngularVelocity);
                if (angSq < kSleepSpeedThresholdSq)
                {
                    if (angSq < kSnapZeroThresholdSq)
                    {
                        rb.AngularVelocity = { 0.0f, 0.0f, 0.0f };
                    }
                    else
                    {
                        rb.AngularVelocity = Mul(rb.AngularVelocity, sleepDamp);
                    }
                }
            }
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

    void PhysicsSystem::SolveConstraints(Scene& scene, f32 dt)
    {
        if (dt <= 0.0f) return;

        auto* trPool = scene.GetPool<TransformComponent>();
        if (!trPool) return;

        auto* rbPool = scene.GetPool<RigidBodyComponent>();
        auto* boxPool = scene.GetPool<BoxColliderComponent>();
        auto* spherePool = scene.GetPool<SphereColliderComponent>();
        auto* capsulePool = scene.GetPool<CapsuleColliderComponent>();

        auto* distJointPool = scene.GetPool<DistanceJointComponent>();
        auto* hingeJointPool = scene.GetPool<HingeJointComponent>();
        auto* fixedJointPool = scene.GetPool<FixedJointComponent>();

        if (!distJointPool && !hingeJointPool && !fixedJointPool) return;

        constexpr int kConstraintIterations = 2;
        constexpr f32 kMaxConstraintAngularSpeed = 20.0f;
        const auto clampAngVel = [](DirectX::XMFLOAT3& v) noexcept {
            const f32 lenSq = Dot(v, v);
            if (lenSq > kMaxConstraintAngularSpeed * kMaxConstraintAngularSpeed)
            {
                const f32 invLen = kMaxConstraintAngularSpeed / std::sqrt(lenSq);
                v = Mul(v, invLen);
            }
        };
        for (int iter = 0; iter < kConstraintIterations; ++iter)
        {
            if (distJointPool)
            {
                const usize count = distJointPool->Size();
                const auto entities = distJointPool->GetEntities();
                auto data = distJointPool->GetData();

                for (usize i = 0; i < count; ++i)
                {
                    Entity entA = scene.GetEntityFromIndex(entities[i]);
                    if (!scene.IsValid(entA)) continue;

                    auto* trA = trPool->TryGet(entA.GetIndex());
                    if (!trA) continue;

                    auto& joint = data[i];
                    const bool hasTarget = scene.IsValid(joint.TargetEntity);
                    auto* trB = hasTarget ? trPool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;

                    const DirectX::XMFLOAT3 worldAnchorA = GetWorldPoint(*trA, joint.AnchorA);
                    const DirectX::XMFLOAT3 worldAnchorB = (hasTarget && trB) ? GetWorldPoint(*trB, joint.AnchorB) : joint.AnchorB;

                    const DirectX::XMFLOAT3 delta = Sub(worldAnchorB, worldAnchorA);
                    const f32 currentDistance = Length(delta);

                    if (joint.AutoDistance)
                    {
                        joint.TargetDistance = currentDistance;
                        joint.AutoDistance = false;
                    }

                    const f32 error = currentDistance - joint.TargetDistance;
                    if (std::abs(error) < 1e-6f) continue;

                    DirectX::XMFLOAT3 n{ 0.0f, 1.0f, 0.0f };
                    if (currentDistance > 1e-6f)
                    {
                        n = Mul(delta, 1.0f / currentDistance);
                    }

                    auto* rbA = rbPool ? rbPool->TryGet(entA.GetIndex()) : nullptr;
                    auto* rbB = (hasTarget && rbPool) ? rbPool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;

                    const bool isDynA = (rbA != nullptr && rbA->Type == BodyType::Dynamic);
                    const bool isDynB = (hasTarget && rbB != nullptr && rbB->Type == BodyType::Dynamic);

                    if (!isDynA && !isDynB) continue;

                    const f32 invMassA = isDynA ? (1.0f / std::max(0.001f, rbA->Mass)) : 0.0f;
                    const f32 invMassB = isDynB ? (1.0f / std::max(0.001f, rbB->Mass)) : 0.0f;
                    const f32 invMassSum = invMassA + invMassB;
                    if (invMassSum < 1e-6f) continue;

                    const f32 stiffness = (joint.Stiffness <= 0.0f) ? 1.0f : std::clamp(joint.Stiffness, 0.0f, 1.0f);
                    const f32 posCorr = error * stiffness;

                    if (isDynA)
                    {
                        trA->Position = Add(trA->Position, Mul(n, posCorr * (invMassA / invMassSum)));
                    }
                    if (isDynB && trB)
                    {
                        trB->Position = Sub(trB->Position, Mul(n, posCorr * (invMassB / invMassSum)));
                    }

                    const DirectX::XMFLOAT3 rA = Sub(worldAnchorA, trA->Position);
                    const DirectX::XMFLOAT3 rB = (hasTarget && trB) ? Sub(worldAnchorB, trB->Position) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };

                    const DirectX::XMFLOAT3 vA = isDynA ? Add(rbA->LinearVelocity, Cross(rbA->AngularVelocity, rA)) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                    const DirectX::XMFLOAT3 vB = isDynB ? Add(rbB->LinearVelocity, Cross(rbB->AngularVelocity, rB)) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                    const f32 relVel = Dot(Sub(vB, vA), n);

                    const DirectX::XMFLOAT3 tA = Cross(rA, n);
                    const auto* boxA = boxPool ? boxPool->TryGet(entA.GetIndex()) : nullptr;
                    const auto* sphereA = spherePool ? spherePool->TryGet(entA.GetIndex()) : nullptr;
                    const auto* capsuleA = capsulePool ? capsulePool->TryGet(entA.GetIndex()) : nullptr;
                    const DirectX::XMFLOAT3 invItA = isDynA ? GetInverseInertiaTensor(tA, *trA, boxA, sphereA, capsuleA, rbA->Mass) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                    const f32 rotA = isDynA ? Dot(tA, invItA) : 0.0f;

                    const auto* boxB = (hasTarget && boxPool) ? boxPool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;
                    const auto* sphereB = (hasTarget && spherePool) ? spherePool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;
                    const auto* capsuleB = (hasTarget && capsulePool) ? capsulePool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;
                    const DirectX::XMFLOAT3 tB = Cross(rB, n);
                    const DirectX::XMFLOAT3 invItB = isDynB ? GetInverseInertiaTensor(tB, *trB, boxB, sphereB, capsuleB, rbB->Mass) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                    const f32 rotB = isDynB ? Dot(tB, invItB) : 0.0f;

                    const f32 effMassInv = invMassSum + rotA + rotB;
                    if (effMassInv > 1e-6f)
                    {
                        const f32 damping = std::clamp(joint.Damping, 0.0f, 1.0f) * relVel;
                        const f32 impulseMag = -(relVel + (stiffness * error) / dt + damping) / effMassInv;
                        const DirectX::XMFLOAT3 impulse = Mul(n, impulseMag);

                        if (isDynA)
                        {
                            rbA->LinearVelocity = Sub(rbA->LinearVelocity, Mul(impulse, invMassA));
                            rbA->AngularVelocity = Sub(rbA->AngularVelocity, Mul(invItA, impulseMag));
                            clampAngVel(rbA->AngularVelocity);
                        }
                        if (isDynB)
                        {
                            rbB->LinearVelocity = Add(rbB->LinearVelocity, Mul(impulse, invMassB));
                            rbB->AngularVelocity = Add(rbB->AngularVelocity, Mul(invItB, impulseMag));
                            clampAngVel(rbB->AngularVelocity);
                        }
                    }
                }
            }

            if (hingeJointPool)
            {
                const usize count = hingeJointPool->Size();
                const auto entities = hingeJointPool->GetEntities();
                auto data = hingeJointPool->GetData();

                for (usize i = 0; i < count; ++i)
                {
                    Entity entA = scene.GetEntityFromIndex(entities[i]);
                    if (!scene.IsValid(entA)) continue;

                    auto* trA = trPool->TryGet(entA.GetIndex());
                    if (!trA) continue;

                    auto& joint = data[i];
                    const bool hasTarget = scene.IsValid(joint.TargetEntity);
                    auto* trB = hasTarget ? trPool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;

                    const DirectX::XMFLOAT3 worldAnchorA = GetWorldPoint(*trA, joint.Anchor);

                    auto& state = m_hingeStates[entA.GetID()];
                    if (!state.Initialized)
                    {
                        if (hasTarget && trB)
                        {
                            const DirectX::XMFLOAT3 delta = Sub(worldAnchorA, trB->Position);
                            const auto invRotB = DirectX::XMMatrixTranspose(DirectX::XMMatrixRotationRollPitchYaw(trB->Rotation.x, trB->Rotation.y, trB->Rotation.z));
                            const auto localB = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(delta.x, delta.y, delta.z, 0.0f), invRotB);
                            const DirectX::XMFLOAT3 scaleB{
                                std::abs(trB->Scale.x) > 1e-4f ? trB->Scale.x : 1.0f,
                                std::abs(trB->Scale.y) > 1e-4f ? trB->Scale.y : 1.0f,
                                std::abs(trB->Scale.z) > 1e-4f ? trB->Scale.z : 1.0f
                            };
                            state.LocalAnchorB = {
                                DirectX::XMVectorGetX(localB) / scaleB.x,
                                DirectX::XMVectorGetY(localB) / scaleB.y,
                                DirectX::XMVectorGetZ(localB) / scaleB.z
                            };
                        }
                        state.WorldAnchor = worldAnchorA;
                        state.Initialized = true;
                    }

                    const DirectX::XMFLOAT3 worldAnchorB = (hasTarget && trB) ? GetWorldPoint(*trB, state.LocalAnchorB) : state.WorldAnchor;

                    auto* rbA = rbPool ? rbPool->TryGet(entA.GetIndex()) : nullptr;
                    auto* rbB = (hasTarget && rbPool) ? rbPool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;

                    const bool isDynA = (rbA != nullptr && rbA->Type == BodyType::Dynamic);
                    const bool isDynB = (hasTarget && rbB != nullptr && rbB->Type == BodyType::Dynamic);

                    if (!isDynA && !isDynB) continue;

                    const f32 invMassA = isDynA ? (1.0f / std::max(0.001f, rbA->Mass)) : 0.0f;
                    const f32 invMassB = isDynB ? (1.0f / std::max(0.001f, rbB->Mass)) : 0.0f;
                    const f32 invMassSum = invMassA + invMassB;
                    if (invMassSum < 1e-6f) continue;

                    const DirectX::XMFLOAT3 posError = Sub(worldAnchorB, worldAnchorA);
                    if (isDynA)
                    {
                        trA->Position = Add(trA->Position, Mul(posError, invMassA / invMassSum));
                    }
                    if (isDynB && trB)
                    {
                        trB->Position = Sub(trB->Position, Mul(posError, invMassB / invMassSum));
                    }

                    const DirectX::XMFLOAT3 rA = Sub(worldAnchorA, trA->Position);
                    const DirectX::XMFLOAT3 rB = (hasTarget && trB) ? Sub(worldAnchorB, trB->Position) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };

                    const DirectX::XMFLOAT3 vA = isDynA ? Add(rbA->LinearVelocity, Cross(rbA->AngularVelocity, rA)) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                    const DirectX::XMFLOAT3 vB = isDynB ? Add(rbB->LinearVelocity, Cross(rbB->AngularVelocity, rB)) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                    const DirectX::XMFLOAT3 vRel = Sub(vB, vA);

                    const DirectX::XMFLOAT3 linImpulse = Mul(Add(vRel, Mul(posError, 0.2f / dt)), 1.0f / invMassSum);

                    const auto* boxA = boxPool ? boxPool->TryGet(entA.GetIndex()) : nullptr;
                    const auto* sphereA = spherePool ? spherePool->TryGet(entA.GetIndex()) : nullptr;
                    const auto* capsuleA = capsulePool ? capsulePool->TryGet(entA.GetIndex()) : nullptr;
                    const auto* boxB = (hasTarget && boxPool) ? boxPool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;
                    const auto* sphereB = (hasTarget && spherePool) ? spherePool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;
                    const auto* capsuleB = (hasTarget && capsulePool) ? capsulePool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;

                    if (isDynA)
                    {
                        rbA->LinearVelocity = Add(rbA->LinearVelocity, Mul(linImpulse, invMassA));
                        rbA->AngularVelocity = Add(rbA->AngularVelocity, GetInverseInertiaTensor(Cross(rA, linImpulse), *trA, boxA, sphereA, capsuleA, rbA->Mass));
                    }
                    if (isDynB)
                    {
                        rbB->LinearVelocity = Sub(rbB->LinearVelocity, Mul(linImpulse, invMassB));
                        rbB->AngularVelocity = Sub(rbB->AngularVelocity, GetInverseInertiaTensor(Cross(rB, Mul(linImpulse, -1.0f)), *trB, boxB, sphereB, capsuleB, rbB->Mass));
                    }

                    const DirectX::XMFLOAT3 axisWorld = GetWorldDirection(*trA, joint.Axis);

                    const DirectX::XMFLOAT3 angVelB = isDynB ? rbB->AngularVelocity : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                    const DirectX::XMFLOAT3 relAngVel = Sub(isDynA ? rbA->AngularVelocity : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f }, angVelB);

                    const f32 projSpeed = Dot(relAngVel, axisWorld);
                    const DirectX::XMFLOAT3 parallelAngVel = Mul(axisWorld, projSpeed);
                    const DirectX::XMFLOAT3 perpAngVel = Sub(relAngVel, parallelAngVel);

                    if (isDynA)
                    {
                        rbA->AngularVelocity = Sub(rbA->AngularVelocity, Mul(perpAngVel, invMassA / invMassSum));
                    }
                    if (isDynB)
                    {
                        rbB->AngularVelocity = Add(rbB->AngularVelocity, Mul(perpAngVel, invMassB / invMassSum));
                    }

                    if (joint.EnableMotor && isDynA)
                    {
                        const f32 currentMotorSpeed = Dot(Sub(rbA->AngularVelocity, angVelB), axisWorld);
                        const f32 speedDiff = joint.MotorSpeed - currentMotorSpeed;
                        f32 motorTorqueImpulse = speedDiff / invMassSum;
                        if (joint.MaxMotorTorque > 0.0f)
                        {
                            const f32 maxImp = joint.MaxMotorTorque * dt;
                            motorTorqueImpulse = std::clamp(motorTorqueImpulse, -maxImp, maxImp);
                        }
                        rbA->AngularVelocity = Add(rbA->AngularVelocity, Mul(axisWorld, motorTorqueImpulse * invMassA));
                        if (isDynB)
                        {
                            rbB->AngularVelocity = Sub(rbB->AngularVelocity, Mul(axisWorld, motorTorqueImpulse * invMassB));
                        }
                    }

                    if (joint.EnableLimits)
                    {
                        DirectX::XMFLOAT3 ref{ 0.0f, 1.0f, 0.0f };
                        if (std::abs(axisWorld.y) > 0.9f)
                        {
                            ref = { 1.0f, 0.0f, 0.0f };
                        }
                        const DirectX::XMFLOAT3 u0 = Normalize(Cross(axisWorld, ref));

                        const DirectX::XMFLOAT3 uA = RotateVector(u0, trA->Rotation);
                        const DirectX::XMFLOAT3 uB = (hasTarget && trB) ? RotateVector(u0, trB->Rotation) : u0;

                        const DirectX::XMFLOAT3 uAproj = Normalize(Sub(uA, Mul(axisWorld, Dot(uA, axisWorld))));
                        const DirectX::XMFLOAT3 uBproj = Normalize(Sub(uB, Mul(axisWorld, Dot(uB, axisWorld))));

                        const f32 cosAngle = std::clamp(Dot(uBproj, uAproj), -1.0f, 1.0f);
                        const f32 sinAngle = Dot(Cross(uBproj, uAproj), axisWorld);
                        const f32 angle = std::atan2(sinAngle, cosAngle);

                        if (angle < joint.LowerAngleLimit)
                        {
                            const f32 angleErr = joint.LowerAngleLimit - angle;
                            if (isDynA)
                            {
                                trA->Rotation = Add(trA->Rotation, Mul(axisWorld, angleErr * (invMassA / invMassSum)));
                            }
                            if (isDynB && trB)
                            {
                                trB->Rotation = Sub(trB->Rotation, Mul(axisWorld, angleErr * (invMassB / invMassSum)));
                            }
                            const f32 speedAlongAxis = Dot(Sub(isDynA ? rbA->AngularVelocity : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f }, angVelB), axisWorld);
                            if (speedAlongAxis < 0.0f)
                            {
                                if (isDynA)
                                {
                                    rbA->AngularVelocity = Sub(rbA->AngularVelocity, Mul(axisWorld, speedAlongAxis * (invMassA / invMassSum)));
                                }
                                if (isDynB)
                                {
                                    rbB->AngularVelocity = Add(rbB->AngularVelocity, Mul(axisWorld, speedAlongAxis * (invMassB / invMassSum)));
                                }
                            }
                        }
                        else if (angle > joint.UpperAngleLimit)
                        {
                            const f32 angleErr = angle - joint.UpperAngleLimit;
                            if (isDynA)
                            {
                                trA->Rotation = Sub(trA->Rotation, Mul(axisWorld, angleErr * (invMassA / invMassSum)));
                            }
                            if (isDynB && trB)
                            {
                                trB->Rotation = Add(trB->Rotation, Mul(axisWorld, angleErr * (invMassB / invMassSum)));
                            }
                            const f32 speedAlongAxis = Dot(Sub(isDynA ? rbA->AngularVelocity : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f }, angVelB), axisWorld);
                            if (speedAlongAxis > 0.0f)
                            {
                                if (isDynA)
                                {
                                    rbA->AngularVelocity = Sub(rbA->AngularVelocity, Mul(axisWorld, speedAlongAxis * (invMassA / invMassSum)));
                                    clampAngVel(rbA->AngularVelocity);
                                }
                                if (isDynB)
                                {
                                    rbB->AngularVelocity = Add(rbB->AngularVelocity, Mul(axisWorld, speedAlongAxis * (invMassB / invMassSum)));
                                    clampAngVel(rbB->AngularVelocity);
                                }
                            }
                        }
                    }
                    if (isDynA) clampAngVel(rbA->AngularVelocity);
                    if (isDynB) clampAngVel(rbB->AngularVelocity);
                }
            }

            if (fixedJointPool)
            {
                const usize count = fixedJointPool->Size();
                const auto entities = fixedJointPool->GetEntities();
                auto data = fixedJointPool->GetData();

                for (usize i = 0; i < count; ++i)
                {
                    auto& joint = data[i];
                    if (joint.IsBroken) continue;

                    Entity entA = scene.GetEntityFromIndex(entities[i]);
                    if (!scene.IsValid(entA)) continue;

                    auto* trA = trPool->TryGet(entA.GetIndex());
                    if (!trA) continue;

                    const bool hasTarget = scene.IsValid(joint.TargetEntity);
                    auto* trB = hasTarget ? trPool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;

                    DirectX::XMFLOAT3 targetPos = joint.RelativeOffset;
                    DirectX::XMFLOAT3 targetRot = joint.RelativeRotation;
                    DirectX::XMFLOAT3 targetLinVel{ 0.0f, 0.0f, 0.0f };
                    DirectX::XMFLOAT3 targetAngVel{ 0.0f, 0.0f, 0.0f };

                    auto* rbA = rbPool ? rbPool->TryGet(entA.GetIndex()) : nullptr;
                    auto* rbB = (hasTarget && rbPool) ? rbPool->TryGet(joint.TargetEntity.GetIndex()) : nullptr;

                    const bool isDynA = (rbA != nullptr && rbA->Type == BodyType::Dynamic);
                    const bool isDynB = (hasTarget && rbB != nullptr && rbB->Type == BodyType::Dynamic);

                    if (hasTarget && trB)
                    {
                        targetRot = Add(trB->Rotation, joint.RelativeRotation);
                        const DirectX::XMFLOAT3 rotatedOffset = RotateVector(joint.RelativeOffset, trB->Rotation);
                        targetPos = Add(trB->Position, rotatedOffset);
                        if (isDynB)
                        {
                            targetLinVel = Add(rbB->LinearVelocity, Cross(rbB->AngularVelocity, rotatedOffset));
                            targetAngVel = rbB->AngularVelocity;
                        }
                    }

                    const DirectX::XMFLOAT3 posError = Sub(targetPos, trA->Position);
                    DirectX::XMFLOAT3 rotError = Sub(targetRot, trA->Rotation);

                    while (rotError.x > 3.14159265f) rotError.x -= 6.2831853f;
                    while (rotError.x < -3.14159265f) rotError.x += 6.2831853f;
                    while (rotError.y > 3.14159265f) rotError.y -= 6.2831853f;
                    while (rotError.y < -3.14159265f) rotError.y += 6.2831853f;
                    while (rotError.z > 3.14159265f) rotError.z -= 6.2831853f;
                    while (rotError.z < -3.14159265f) rotError.z += 6.2831853f;

                    const f32 invMassA = isDynA ? (1.0f / std::max(0.001f, rbA->Mass)) : 0.0f;
                    const f32 invMassB = isDynB ? (1.0f / std::max(0.001f, rbB->Mass)) : 0.0f;
                    const f32 invMassSum = invMassA + invMassB;

                    if (invMassSum < 1e-6f)
                    {
                        trA->Position = targetPos;
                        trA->Rotation = targetRot;
                        continue;
                    }

                    const DirectX::XMFLOAT3 vA = isDynA ? rbA->LinearVelocity : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                    const DirectX::XMFLOAT3 deltaV = Sub(targetLinVel, vA);

                    const DirectX::XMFLOAT3 linImpulse = Mul(Add(deltaV, Mul(posError, 1.0f / dt)), 1.0f / invMassSum);
                    const f32 impulseMag = Length(linImpulse);

                    if (joint.BreakForce > 0.0f && impulseMag > joint.BreakForce)
                    {
                        joint.IsBroken = true;
                        EventBus::Publish(PhysicsTriggerEvent{
                            entA,
                            joint.TargetEntity,
                            TriggerEvent::State::Exit
                        });
                        RL_LOG_INFO("FixedJoint broke between entity {} and target entity {}", entA.GetIndex(), joint.TargetEntity.GetIndex());
                        continue;
                    }

                    const f32 wA = invMassA / invMassSum;
                    const f32 wB = invMassB / invMassSum;

                    if (isDynA)
                    {
                        trA->Position = Add(trA->Position, Mul(posError, wA));
                        trA->Rotation = Add(trA->Rotation, Mul(rotError, wA));
                        rbA->LinearVelocity = Add(rbA->LinearVelocity, Mul(linImpulse, invMassA));
                        rbA->AngularVelocity = targetAngVel;
                        clampAngVel(rbA->AngularVelocity);
                    }
                    else
                    {
                        trA->Position = targetPos;
                        trA->Rotation = targetRot;
                    }

                    if (isDynB && trB)
                    {
                        trB->Position = Sub(trB->Position, Mul(posError, wB));
                        trB->Rotation = Sub(trB->Rotation, Mul(rotError, wB));
                        rbB->LinearVelocity = Sub(rbB->LinearVelocity, Mul(linImpulse, invMassB));
                        clampAngVel(rbB->AngularVelocity);
                    }
                }
            }
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
        const auto* capsulePool = scene.GetPool<CapsuleColliderComponent>();

        float dirLen = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (dirLen < 1e-6f) return std::nullopt;

        const float dx = direction.x / dirLen;
        const float dy = direction.y / dirLen;
        const float dz = direction.z / dirLen;
        const DirectX::XMFLOAT3 rayDir{ dx, dy, dz };

        float closestDist = maxDistance;
        std::optional<RaycastHit> hit;

        for (const auto& ent : scene.GetAliveEntities())
        {
            const auto* tr = trPool->TryGet(ent.GetIndex());
            if (!tr) continue;

            const auto* box = boxPool ? boxPool->TryGet(ent.GetIndex()) : nullptr;
            const auto* sphere = spherePool ? spherePool->TryGet(ent.GetIndex()) : nullptr;
            const auto* capsule = capsulePool ? capsulePool->TryGet(ent.GetIndex()) : nullptr;

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
            else if (capsule && !box && !sphere)
            {
                const Capsule cap = BuildCapsule(*tr, capsule);
                const DirectX::XMFLOAT3 closestOnSeg = ClosestPointOnSegment(origin, cap.A, cap.B);
                const DirectX::XMFLOAT3 diff0 = Sub(origin, closestOnSeg);
                if (LengthSq(diff0) <= cap.Radius * cap.Radius)
                {
                    if (0.0f < closestDist)
                    {
                        closestDist = 0.0f;
                        RaycastHit h;
                        h.HitEntity = ent;
                        h.Point = origin;
                        h.Normal = (LengthSq(diff0) > 1e-8f) ? Normalize(diff0) : DirectX::XMFLOAT3{ -dx, -dy, -dz };
                        h.Distance = 0.0f;
                        hit = h;
                    }
                    continue;
                }

                float bestT = closestDist;
                DirectX::XMFLOAT3 bestNormal{ 0.0f, 1.0f, 0.0f };
                bool hitCap = false;

                const DirectX::XMFLOAT3 ab = Sub(cap.B, cap.A);
                const float hLen = Length(ab);
                if (hLen > 1e-6f)
                {
                    const DirectX::XMFLOAT3 u = Mul(ab, 1.0f / hLen);
                    const DirectX::XMFLOAT3 ao = Sub(origin, cap.A);
                    const DirectX::XMFLOAT3 dPerp = Sub(rayDir, Mul(u, Dot(rayDir, u)));
                    const DirectX::XMFLOAT3 aoPerp = Sub(ao, Mul(u, Dot(ao, u)));

                    const float a = Dot(dPerp, dPerp);
                    const float b = Dot(aoPerp, dPerp);
                    const float c = Dot(aoPerp, aoPerp) - cap.Radius * cap.Radius;
                    if (a > 1e-8f)
                    {
                        const float disc = b * b - a * c;
                        if (disc >= 0.0f)
                        {
                            const float tCyl = (-b - std::sqrt(disc)) / a;
                            if (tCyl >= 0.0f && tCyl < bestT)
                            {
                                const DirectX::XMFLOAT3 pt = Add(origin, Mul(rayDir, tCyl));
                                const float s = Dot(Sub(pt, cap.A), u);
                                if (s >= 0.0f && s <= hLen)
                                {
                                    bestT = tCyl;
                                    const DirectX::XMFLOAT3 axisPt = Add(cap.A, Mul(u, s));
                                    bestNormal = Normalize(Sub(pt, axisPt));
                                    hitCap = true;
                                }
                            }
                        }
                    }
                }

                const DirectX::XMFLOAT3 caps[2] = { cap.A, cap.B };
                for (int k = 0; k < 2; ++k)
                {
                    const DirectX::XMFLOAT3 oc = Sub(origin, caps[k]);
                    const float bS = Dot(oc, rayDir);
                    const float cS = Dot(oc, oc) - cap.Radius * cap.Radius;
                    if (cS > 0.0f && bS > 0.0f) continue;

                    const float discS = bS * bS - cS;
                    if (discS < 0.0f) continue;

                    float tS = -bS - std::sqrt(discS);
                    if (tS < 0.0f) tS = 0.0f;
                    if (tS < bestT)
                    {
                        bestT = tS;
                        const DirectX::XMFLOAT3 pt = Add(origin, Mul(rayDir, tS));
                        const DirectX::XMFLOAT3 n = Sub(pt, caps[k]);
                        bestNormal = (LengthSq(n) > 1e-8f) ? Normalize(n) : DirectX::XMFLOAT3{ -dx, -dy, -dz };
                        hitCap = true;
                    }
                }

                if (hitCap)
                {
                    closestDist = bestT;
                    RaycastHit h;
                    h.HitEntity = ent;
                    h.Point = Add(origin, Mul(rayDir, bestT));
                    h.Normal = bestNormal;
                    h.Distance = bestT;
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

    std::vector<Entity> PhysicsSystem::SpawnRagdollChain(
        Scene& scene,
        const DirectX::XMFLOAT3& origin)
    {
        std::vector<Entity> created;
        created.reserve(4);

        Entity root = scene.CreateEntity("Ragdoll_Root");
        if (auto* tr = scene.TryGetComponent<TransformComponent>(root))
        {
            tr->Position = origin;
            tr->Scale = { 0.5f, 0.6f, 0.5f };
        }
        else
        {
            scene.AddComponent<TransformComponent>(root, origin, DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f }, DirectX::XMFLOAT3{ 0.5f, 0.6f, 0.5f });
        }
        auto& rootMesh = scene.AddComponent<MeshComponent>(root);
        rootMesh.MeshName = "Sphere";
        rootMesh.Color = { 0.95f, 0.85f, 0.45f, 1.0f };
        auto& rootRb = scene.AddComponent<RigidBodyComponent>(root);
        rootRb.Type = BodyType::Dynamic;
        rootRb.Mass = 2.0f;
        rootRb.Restitution = 0.25f;
        rootRb.Friction = 0.5f;
        rootRb.LinearDamping = 0.08f;
        rootRb.AngularDamping = 0.12f;
        auto& rootCap = scene.AddComponent<CapsuleColliderComponent>(root);
        rootCap.Radius = 0.45f;
        rootCap.Height = 1.0f;
        created.push_back(root);

        Entity torso = scene.CreateEntity("Ragdoll_Torso");
        const DirectX::XMFLOAT3 torsoPos{ origin.x, origin.y - 1.05f, origin.z };
        if (auto* tr = scene.TryGetComponent<TransformComponent>(torso))
        {
            tr->Position = torsoPos;
            tr->Scale = { 0.7f, 1.3f, 0.45f };
        }
        else
        {
            scene.AddComponent<TransformComponent>(torso, torsoPos, DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f }, DirectX::XMFLOAT3{ 0.7f, 1.3f, 0.45f });
        }
        auto& torsoMesh = scene.AddComponent<MeshComponent>(torso);
        torsoMesh.MeshName = "Cube";
        torsoMesh.Color = { 0.25f, 0.65f, 0.95f, 1.0f };
        auto& torsoRb = scene.AddComponent<RigidBodyComponent>(torso);
        torsoRb.Type = BodyType::Dynamic;
        torsoRb.Mass = 3.0f;
        torsoRb.Restitution = 0.25f;
        torsoRb.Friction = 0.5f;
        torsoRb.LinearDamping = 0.08f;
        torsoRb.AngularDamping = 0.12f;
        auto& torsoCap = scene.AddComponent<CapsuleColliderComponent>(torso);
        torsoCap.Radius = 0.4f;
        torsoCap.Height = 1.0f;
        auto& torsoJoint = scene.AddComponent<DistanceJointComponent>(torso);
        torsoJoint.TargetEntity = root;
        torsoJoint.AnchorA = { 0.0f, 0.5f, 0.0f };
        torsoJoint.AnchorB = { 0.0f, -0.5f, 0.0f };
        torsoJoint.TargetDistance = 0.1f;
        torsoJoint.Stiffness = 0.9f;
        torsoJoint.Damping = 0.2f;
        torsoJoint.AutoDistance = true;
        created.push_back(torso);

        Entity armL = scene.CreateEntity("Ragdoll_ArmL");
        const DirectX::XMFLOAT3 armLPos{ origin.x - 0.62f, origin.y - 1.05f, origin.z };
        if (auto* tr = scene.TryGetComponent<TransformComponent>(armL))
        {
            tr->Position = armLPos;
            tr->Scale = { 0.35f, 1.0f, 0.35f };
        }
        else
        {
            scene.AddComponent<TransformComponent>(armL, armLPos, DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f }, DirectX::XMFLOAT3{ 0.35f, 1.0f, 0.35f });
        }
        auto& armLMesh = scene.AddComponent<MeshComponent>(armL);
        armLMesh.MeshName = "Cube";
        armLMesh.Color = { 0.95f, 0.45f, 0.35f, 1.0f };
        auto& armLRb = scene.AddComponent<RigidBodyComponent>(armL);
        armLRb.Type = BodyType::Dynamic;
        armLRb.Mass = 1.0f;
        armLRb.Restitution = 0.25f;
        armLRb.Friction = 0.5f;
        armLRb.LinearDamping = 0.08f;
        armLRb.AngularDamping = 0.12f;
        auto& armLCap = scene.AddComponent<CapsuleColliderComponent>(armL);
        armLCap.Radius = 0.4f;
        armLCap.Height = 1.0f;
        auto& armLHinge = scene.AddComponent<HingeJointComponent>(armL);
        armLHinge.TargetEntity = torso;
        armLHinge.Anchor = { 0.0f, 0.45f, 0.0f };
        armLHinge.Axis = { 0.0f, 0.0f, 1.0f };
        armLHinge.LowerAngleLimit = -1.57f;
        armLHinge.UpperAngleLimit = 1.57f;
        armLHinge.EnableLimits = true;
        created.push_back(armL);

        Entity armR = scene.CreateEntity("Ragdoll_ArmR");
        const DirectX::XMFLOAT3 armRPos{ origin.x + 0.62f, origin.y - 1.05f, origin.z };
        if (auto* tr = scene.TryGetComponent<TransformComponent>(armR))
        {
            tr->Position = armRPos;
            tr->Scale = { 0.35f, 1.0f, 0.35f };
        }
        else
        {
            scene.AddComponent<TransformComponent>(armR, armRPos, DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f }, DirectX::XMFLOAT3{ 0.35f, 1.0f, 0.35f });
        }
        auto& armRMesh = scene.AddComponent<MeshComponent>(armR);
        armRMesh.MeshName = "Cube";
        armRMesh.Color = { 0.95f, 0.45f, 0.35f, 1.0f };
        auto& armRRb = scene.AddComponent<RigidBodyComponent>(armR);
        armRRb.Type = BodyType::Dynamic;
        armRRb.Mass = 1.0f;
        armRRb.Restitution = 0.25f;
        armRRb.Friction = 0.5f;
        armRRb.LinearDamping = 0.08f;
        armRRb.AngularDamping = 0.12f;
        auto& armRCap = scene.AddComponent<CapsuleColliderComponent>(armR);
        armRCap.Radius = 0.4f;
        armRCap.Height = 1.0f;
        auto& armRHinge = scene.AddComponent<HingeJointComponent>(armR);
        armRHinge.TargetEntity = torso;
        armRHinge.Anchor = { 0.0f, 0.45f, 0.0f };
        armRHinge.Axis = { 0.0f, 0.0f, 1.0f };
        armRHinge.LowerAngleLimit = -1.57f;
        armRHinge.UpperAngleLimit = 1.57f;
        armRHinge.EnableLimits = true;
        created.push_back(armR);

        return created;
    }
}
