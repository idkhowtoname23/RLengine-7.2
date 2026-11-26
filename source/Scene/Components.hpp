#pragma once

#include "Core/Types.hpp"
#include "Core/UUID.hpp"
#include "Scene/Entity.hpp"
#include <DirectXMath.h>
#include <string>
#include <memory>
#include <vector>

namespace RL
{
    struct RelationshipComponent
    {
        Entity Parent{};
        std::vector<Entity> Children{};
    };
    using HierarchyComponent = RelationshipComponent;

    struct IDComponent
    {
        UUID ID = UUID::Generate();
    };

    struct ScriptComponent
    {
        std::string ScriptName = "";
        std::string ScriptPath = "";
        bool Enabled = true;
    };

    struct TagComponent
    {
        std::string Tag = "Entity";
    };

    struct TransformComponent
    {
        DirectX::XMFLOAT3 Position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Rotation{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Scale{ 1.0f, 1.0f, 1.0f };

        [[nodiscard]] DirectX::XMMATRIX GetWorldMatrix() const noexcept
        {
            const auto S = DirectX::XMMatrixScaling(Scale.x, Scale.y, Scale.z);
            const auto R = DirectX::XMMatrixRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z);
            const auto T = DirectX::XMMatrixTranslation(Position.x, Position.y, Position.z);
            return S * R * T;
        }

        [[nodiscard]] DirectX::XMMATRIX GetLocalMatrix() const noexcept
        {
            return GetWorldMatrix();
        }
    };

    struct QuadRenderComponent
    {
        DirectX::XMFLOAT4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool IsVisible = true;
        u32 TextureIndex = 0;
        std::string TexturePath = "";
    };

    class Mesh;

    struct MeshComponent
    {
        std::string MeshName = "Cube";
        std::string ModelPath = "";
        std::shared_ptr<Mesh> MeshRef = nullptr;
        DirectX::XMFLOAT4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        u32 TextureIndex = 0;
        std::string TexturePath = "";
        bool IsVisible = true;
        bool Wireframe = false;
    };

    enum class LightType : u8
    {
        Point = 0,
        Directional,
        Spot
    };

    struct LightComponent
    {
        LightType Type = LightType::Point;
        DirectX::XMFLOAT3 Color{ 1.0f, 1.0f, 1.0f };
        f32 Intensity = 1.0f;
        f32 Range = 10.0f;
        f32 SpotAngle = 45.0f;
        bool CastShadows = false;
    };

    struct MaterialComponent
    {
        DirectX::XMFLOAT4 BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 AlbedoColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        f32 Metallic = 0.0f;
        f32 Roughness = 0.5f;
        f32 AO = 1.0f;
        f32 NormalStrength = 1.0f;
        f32 EmissionStrength = 0.0f;
        DirectX::XMFLOAT3 EmissiveColor{ 0.0f, 0.0f, 0.0f };
        f32 EmissiveIntensity = 0.0f;
        u32 TextureIndex = 0;
        std::string TexturePath = "";
        u32 NormalMapIndex = 0;
        std::string NormalMapPath = "";
        u32 MetallicRoughnessMapIndex = 0;
        std::string MetallicRoughnessMapPath = "";
        u32 EmissiveMapIndex = 0;
        std::string EmissiveMapPath = "";
    };

    struct CameraComponent
    {
        f32 Fov = 60.0f;
        f32 NearZ = 0.1f;
        f32 FarZ = 1000.0f;
        bool IsOrthographic = false;
        f32 OrthoSize = 10.0f;
        bool IsPrimary = true;
    };

    enum class BodyType : u8
    {
        Static = 0,
        Dynamic,
        Kinematic
    };

    struct RigidBodyComponent
    {
        BodyType Type = BodyType::Dynamic;
        f32 Mass = 1.0f;
        DirectX::XMFLOAT3 LinearVelocity{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 AngularVelocity{ 0.0f, 0.0f, 0.0f };
        f32 GravityScale = 1.0f;
        f32 Restitution = 0.5f;
        f32 Friction = 0.3f;
        f32 LinearDamping = 0.02f;
        f32 AngularDamping = 0.05f;
        bool UseGravity = true;
        bool IsGrounded = false;
    };

    struct BoxColliderComponent
    {
        DirectX::XMFLOAT3 HalfSize{ 0.5f, 0.5f, 0.5f };
        DirectX::XMFLOAT3 Offset{ 0.0f, 0.0f, 0.0f };
        bool IsTrigger = false;
    };

    struct SphereColliderComponent
    {
        f32 Radius = 0.5f;
        DirectX::XMFLOAT3 Offset{ 0.0f, 0.0f, 0.0f };
        bool IsTrigger = false;
    };

    struct CapsuleColliderComponent
    {
        f32 Radius = 0.4f;
        f32 Height = 1.8f;
        DirectX::XMFLOAT3 Offset{ 0.0f, 0.0f, 0.0f };
        bool IsTrigger = false;
    };

    struct DistanceJointComponent
    {
        Entity TargetEntity{};
        DirectX::XMFLOAT3 AnchorA{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 AnchorB{ 0.0f, 0.0f, 0.0f };
        f32 TargetDistance = 1.0f;
        f32 Stiffness = 1.0f;
        f32 Damping = 0.1f;
        bool AutoDistance = true;
    };

    struct HingeJointComponent
    {
        Entity TargetEntity{};
        DirectX::XMFLOAT3 Anchor{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Axis{ 0.0f, 1.0f, 0.0f };
        f32 LowerAngleLimit = -3.14159265f;
        f32 UpperAngleLimit = 3.14159265f;
        bool EnableLimits = false;
        f32 MotorSpeed = 0.0f;
        f32 MaxMotorTorque = 0.0f;
        bool EnableMotor = false;
    };

    struct FixedJointComponent
    {
        Entity TargetEntity{};
        DirectX::XMFLOAT3 RelativeOffset{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 RelativeRotation{ 0.0f, 0.0f, 0.0f };
        f32 BreakForce = 0.0f;
        bool IsBroken = false;
    };

    struct ParticleEmitterComponent
    {
        u32 MaxParticles = 1024;
        f32 EmissionRate = 120.0f;
        f32 Lifetime = 2.0f;
        f32 Speed = 3.5f;
        f32 ConeAngle = 45.0f;
        f32 StartSize = 0.12f;
        f32 EndSize = 0.01f;
        DirectX::XMFLOAT4 StartColor{ 1.0f, 0.6f, 0.1f, 1.0f };
        DirectX::XMFLOAT4 EndColor{ 0.8f, 0.1f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 InitialVelocity{ 0.0f, 3.5f, 0.0f };
        DirectX::XMFLOAT3 VelocitySpread{ 1.5f, 0.8f, 1.5f };
        DirectX::XMFLOAT3 Gravity{ 0.0f, -4.9f, 0.0f };
        bool FloorBounce = true;
        bool Looping = true;
        bool IsActive = true;
    };

    struct SkinnedMeshComponent
    {
        std::string ModelPath = "";
        std::string SkeletonName = "";
        std::string CurrentAnimation = "";
        std::string PreviousAnimation = "";
        f32 AnimationTime = 0.0f;
        f32 PreviousAnimationTime = 0.0f;
        f32 AnimationSpeed = 1.0f;
        f32 BlendTime = 0.0f;
        f32 BlendDuration = 0.0f;
        bool Loop = true;
        bool IsPlaying = true;
        bool IsVisible = true;
        bool EnableIK = false;
        u32 IKRootBone = 0;
        u32 IKMidBone = 1;
        u32 IKEndBone = 2;
        DirectX::XMFLOAT3 IKTargetPos{ 0.0f, 0.0f, 0.0f };
        f32 IKWeight = 1.0f;
        std::vector<DirectX::XMFLOAT4X4> BonePalette;

        void Play(std::string_view animName)
        {
            PreviousAnimation.clear();
            PreviousAnimationTime = 0.0f;
            BlendTime = 0.0f;
            BlendDuration = 0.0f;
            CurrentAnimation = animName;
            AnimationTime = 0.0f;
            IsPlaying = true;
        }

        void CrossFade(std::string_view animName, f32 duration)
        {
            if (CurrentAnimation == animName)
            {
                return;
            }
            if (duration <= 0.0001f || CurrentAnimation.empty())
            {
                Play(animName);
                return;
            }
            PreviousAnimation = CurrentAnimation;
            PreviousAnimationTime = AnimationTime;
            BlendTime = 0.0f;
            BlendDuration = duration;
            CurrentAnimation = animName;
            AnimationTime = 0.0f;
            IsPlaying = true;
        }

        void Update(f32 dt)
        {
            if (IsPlaying)
            {
                AnimationTime += dt * AnimationSpeed;
                if (BlendDuration > 0.0f)
                {
                    PreviousAnimationTime += dt * AnimationSpeed;
                    BlendTime += dt;
                    if (BlendTime >= BlendDuration)
                    {
                        PreviousAnimation.clear();
                        PreviousAnimationTime = 0.0f;
                        BlendTime = 0.0f;
                        BlendDuration = 0.0f;
                    }
                }
            }
        }
    };

    struct AudioSourceComponent
    {
        std::string SoundPath = "";
        f32 Volume = 1.0f;
        f32 Pitch = 1.0f;
        f32 MinDistance = 1.0f;
        f32 MaxDistance = 50.0f;
        bool Is3D = true;
        bool Loop = false;
        bool PlayOnStart = false;
        bool IsPlaying = false;
    };

    struct AudioListenerComponent
    {
        bool IsActive = true;
    };
}
