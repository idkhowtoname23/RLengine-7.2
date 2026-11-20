#pragma once

#include "Core/Types.hpp"
#include "Scene/Entity.hpp"
#include <unordered_map>

namespace RL
{
    class Scene;

    class AudioSystem
    {
    public:
        AudioSystem() = default;
        ~AudioSystem();

        void Update(Scene& scene);
        void Update(Scene& scene, f32 dt);

        void StopEntitySound(Entity entity) noexcept;
        [[nodiscard]] bool IsEntitySoundPlaying(Entity entity) const noexcept;
        [[nodiscard]] bool HasTrackedSound(Entity entity) const noexcept;
        void SetEntitySoundVolume(Entity entity, float volume) noexcept;

    private:
        std::unordered_map<u32, u32> m_entitySounds;
    };
}
