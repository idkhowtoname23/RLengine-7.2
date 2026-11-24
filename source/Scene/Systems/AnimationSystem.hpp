#pragma once

#include "Core/Types.hpp"
#include "Graphics/DX12/GLTFLoader.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace RL
{
    class Scene;

    struct ModelRig
    {
        std::vector<Skeleton> Skeletons;
        std::vector<AnimationClip> Clips;
    };

    class AnimationSystem
    {
    public:
        static constexpr usize MaxBones = 128;

        AnimationSystem() = default;
        ~AnimationSystem() = default;

        void RegisterModelRig(
            std::string_view key,
            const std::vector<Skeleton>& skeletons,
            const std::vector<AnimationClip>& clips);

        [[nodiscard]] bool HasModelRig(std::string_view key) const;
        [[nodiscard]] const ModelRig* GetModelRig(std::string_view key) const;
        void ClearRigs() noexcept;

        void CreateProceduralTestRig(std::string_view key = "__procedural_test_rig__");

        void Update(Scene& scene, f32 dt);

    private:
        std::unordered_map<std::string, ModelRig> m_rigs;
    };
}
