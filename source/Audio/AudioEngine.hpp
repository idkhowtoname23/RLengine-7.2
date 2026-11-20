#pragma once

#include "Core/Types.hpp"
#include <string>
#include <string_view>
#include <expected>
#include <memory>
#include <vector>
#include <unordered_map>

struct ma_sound;

namespace RL
{
    class AudioEngine
    {
    public:
        using SoundID = u32;

        static AudioEngine& Get() noexcept;

        AudioEngine();
        ~AudioEngine();

        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        [[nodiscard]] std::expected<void, std::string> Initialize();
        void Shutdown() noexcept;

        void Update() noexcept;

        void SetListenerPosition(float x, float y, float z) noexcept;
        void SetListenerDirection(float forwardX, float forwardY, float forwardZ, float upX = 0.0f, float upY = 1.0f, float upZ = 0.0f) noexcept;

        SoundID PlaySpatialSound(const std::string& path, float x, float y, float z, float volume = 1.0f, float pitch = 1.0f, float minDistance = 1.0f, float maxDistance = 50.0f, bool loop = false);
        SoundID PlaySound2D(const std::string& path, float volume = 1.0f, float pitch = 1.0f, bool loop = false);

        void PlaySound(std::string_view filepath, f32 volume = 1.0f);
        void PlaySound3D(std::string_view filepath, f32 x, f32 y, f32 z, f32 volume = 1.0f, f32 minDistance = 1.0f, f32 maxDistance = 50.0f);

        void StopSound(SoundID id) noexcept;
        [[nodiscard]] bool IsSoundPlaying(SoundID id) noexcept;
        void SetSoundPosition(SoundID id, float x, float y, float z) noexcept;
        void SetSoundVolume(SoundID id, float volume) noexcept;
        void SetSoundPitch(SoundID id, float pitch) noexcept;
        void StopAllSounds() noexcept;

        void SetMasterVolume(f32 volume) noexcept;
        [[nodiscard]] f32 GetMasterVolume() const noexcept { return m_masterVolume; }
        [[nodiscard]] bool IsInitialized() const noexcept { return m_isInitialized; }

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
        bool m_isInitialized = false;
        f32 m_masterVolume = 1.0f;
    };
}
