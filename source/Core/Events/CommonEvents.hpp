#pragma once

#include "Core/Types.hpp"
#include "Core/StringID.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Systems/PhysicsSystem.hpp"
#include <DirectXMath.h>

#include <string>

namespace RL
{

    struct WindowResizedEvent
    {
        u32 width;
        u32 height;
    };

    struct WindowClosedEvent {};

    struct WindowFocusChangedEvent
    {
        bool focused;
    };

    struct KeyPressedEvent
    {
        i32 keyCode;
        bool repeated;
    };

    struct KeyReleasedEvent
    {
        i32 keyCode;
    };

    struct MouseButtonPressedEvent
    {
        i32 button;
        f32 x, y;
    };

    struct MouseButtonReleasedEvent
    {
        i32 button;
        f32 x, y;
    };

    struct MouseMovedEvent
    {
        f32 x, y;
        f32 deltaX, deltaY;
    };

    struct MouseScrolledEvent
    {
        f32 offsetX;
        f32 offsetY;
    };

    struct EntityCreatedEvent
    {
        Entity entity{};
        u64 entityID = 0;
        StringID tag{};
    };

    struct EntityDestroyedEvent
    {
        Entity entity{};
        u64 entityID = 0;
    };

    struct CollisionEvent
    {
        Entity entityA{};
        Entity entityB{};
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT3 point{ 0.0f, 0.0f, 0.0f };
        f32 impulse = 0.0f;
    };

    struct PhysicsTriggerEvent
    {
        Entity triggerEntity{};
        Entity otherEntity{};
        TriggerEvent::State state = TriggerEvent::State::Enter;
    };

    struct ScriptReloadEvent
    {
        std::string scriptPath = "";
    };

    struct SceneLoadedEvent
    {
        std::string sceneName;
    };

    struct SceneUnloadedEvent
    {
        std::string sceneName;
    };

    struct SimulationStartedEvent {};
    struct SimulationPausedEvent  {};
    struct SimulationStoppedEvent {};

    struct AssetReloadedEvent
    {
        std::string assetPath;
        StringID    assetType;
    };

    struct EngineShutdownEvent {};

}
