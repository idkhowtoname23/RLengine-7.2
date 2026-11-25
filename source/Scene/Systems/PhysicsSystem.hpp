#pragma once

#include "Core/Types.hpp"
#include "Scene/Entity.hpp"
#include <DirectXMath.h>
#include <vector>
#include <unordered_map>
#include <optional>

namespace RL
{
    class Scene;

    struct RaycastHit
    {
        Entity HitEntity{};
        DirectX::XMFLOAT3 Point{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Normal{ 0.0f, 1.0f, 0.0f };
        f32 Distance = 0.0f;
    };

    struct TriggerEvent
    {
        Entity TriggerEntity{};
        Entity OtherEntity{};
        enum class State : u8
        {
            Enter = 0,
            Stay,
            Exit
        };
        State EventState = State::Enter;
    };

    struct TriggerPair
    {
        Entity TriggerEntity{};
        Entity OtherEntity{};

        bool operator==(const TriggerPair& other) const noexcept
        {
            return TriggerEntity == other.TriggerEntity && OtherEntity == other.OtherEntity;
        }
    };

    class PhysicsSystem
    {
    public:
        PhysicsSystem() = default;
        ~PhysicsSystem() = default;

        void Step(Scene& scene, f32 dt);
        void SetGravity(const DirectX::XMFLOAT3& g) noexcept { m_gravity = g; }
        [[nodiscard]] const DirectX::XMFLOAT3& GetGravity() const noexcept { return m_gravity; }

        void SetCCDEnabled(bool enabled) noexcept { m_ccdEnabled = enabled; }
        [[nodiscard]] bool IsCCDEnabled() const noexcept { return m_ccdEnabled; }

        [[nodiscard]] const std::vector<TriggerEvent>& GetTriggerEvents() const noexcept { return m_triggerEvents; }
        [[nodiscard]] const std::vector<TriggerPair>& GetActiveTriggerPairs() const noexcept { return m_activeTriggerPairs; }
        [[nodiscard]] bool HasTriggerOverlap(Entity triggerEntity, Entity otherEntity) const noexcept;
        [[nodiscard]] bool IsTriggerActive(Entity triggerEntity) const noexcept;
        [[nodiscard]] std::vector<Entity> GetEntitiesInTrigger(Entity triggerEntity) const;
        void ClearTriggers() noexcept;

        [[nodiscard]] std::optional<RaycastHit> Raycast(
            const Scene& scene,
            const DirectX::XMFLOAT3& origin,
            const DirectX::XMFLOAT3& direction,
            f32 maxDistance);

        static std::vector<Entity> SpawnRagdollChain(
            Scene& scene,
            const DirectX::XMFLOAT3& origin = { 0.0f, 3.5f, 0.0f });

        void CaptureSnapshot(Scene& scene);
        void RestoreSnapshot(Scene& scene);
        void ClearSnapshot() noexcept;

        void SolveConstraints(Scene& scene, f32 dt);

    private:
        void Integrate(Scene& scene, f32 dt);

        struct HingeJointState
        {
            DirectX::XMFLOAT3 LocalAnchorB{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 WorldAnchor{ 0.0f, 0.0f, 0.0f };
            bool Initialized = false;
        };

        static constexpr f32 FixedTimeStep = 1.0f / 60.0f;
        f32 m_accumulator = 0.0f;
        bool m_ccdEnabled = true;
        DirectX::XMFLOAT3 m_gravity{ 0.0f, -9.81f, 0.0f };
        std::unordered_map<u64, DirectX::XMFLOAT3> m_initialTransforms;
        std::unordered_map<u64, DirectX::XMFLOAT3> m_initialRotations;
        std::unordered_map<u64, HingeJointState> m_hingeStates;
        std::vector<TriggerPair> m_activeTriggerPairs;
        std::vector<TriggerEvent> m_triggerEvents;
    };
}
