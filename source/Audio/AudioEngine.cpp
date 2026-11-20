#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4245 4310 4456 4457 4701 4706 4100)
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "vendor/miniaudio.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "Audio/AudioEngine.hpp"
#include "Core/Logger.hpp"

#include <format>
#include <algorithm>
#include <unordered_map>

namespace RL
{
    struct SoundEntry
    {
        std::unique_ptr<ma_sound> sound;
        bool isSpatial = false;
    };

    struct AudioEngine::Impl
    {
        ma_engine engine{};
        std::unordered_map<SoundID, SoundEntry> activeSounds;
        SoundID nextSoundId = 1;
    };

    AudioEngine& AudioEngine::Get() noexcept
    {
        static AudioEngine instance;
        return instance;
    }

    AudioEngine::AudioEngine() = default;

    AudioEngine::~AudioEngine()
    {
        Shutdown();
    }

    std::expected<void, std::string> AudioEngine::Initialize()
    {
        if (m_isInitialized) return {};

        m_impl = std::make_unique<Impl>();
        ma_result res = ma_engine_init(nullptr, &m_impl->engine);
        if (res != MA_SUCCESS)
        {
            m_impl.reset();
            return std::unexpected(std::format("Failed to initialize miniaudio engine: error code {}", static_cast<int>(res)));
        }

        m_isInitialized = true;
        RL_LOG_INFO("[AudioEngine] 3D Spatial Audio subsystem initialized successfully via miniaudio.");
        return {};
    }

    void AudioEngine::Shutdown() noexcept
    {
        if (!m_isInitialized || !m_impl) return;

        StopAllSounds();

        ma_engine_uninit(&m_impl->engine);
        m_impl.reset();
        m_isInitialized = false;
        RL_LOG_INFO("[AudioEngine] Audio subsystem shut down cleanly.");
    }

    void AudioEngine::Update() noexcept
    {
        if (!m_isInitialized || !m_impl) return;

        for (auto it = m_impl->activeSounds.begin(); it != m_impl->activeSounds.end(); )
        {
            auto* snd = it->second.sound.get();
            if (!snd || ma_sound_at_end(snd) || (!ma_sound_is_looping(snd) && !ma_sound_is_playing(snd)))
            {
                if (snd)
                {
                    ma_sound_uninit(snd);
                }
                it = m_impl->activeSounds.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void AudioEngine::SetListenerPosition(float x, float y, float z) noexcept
    {
        if (!m_isInitialized || !m_impl) return;
        ma_engine_listener_set_position(&m_impl->engine, 0, x, y, z);
    }

    void AudioEngine::SetListenerDirection(float forwardX, float forwardY, float forwardZ, float upX, float upY, float upZ) noexcept
    {
        if (!m_isInitialized || !m_impl) return;
        if (forwardX == 0.0f && forwardY == 0.0f && forwardZ == 0.0f)
        {
            forwardZ = 1.0f;
        }
        if (upX == 0.0f && upY == 0.0f && upZ == 0.0f)
        {
            upY = 1.0f;
        }
        ma_engine_listener_set_direction(&m_impl->engine, 0, forwardX, forwardY, forwardZ);
        ma_engine_listener_set_world_up(&m_impl->engine, 0, upX, upY, upZ);
    }

    AudioEngine::SoundID AudioEngine::PlaySpatialSound(
        const std::string& path,
        float x, float y, float z,
        float volume,
        float pitch,
        float minDistance,
        float maxDistance,
        bool loop)
    {
        if (!m_isInitialized || !m_impl || path.empty()) return 0;

        auto sound = std::make_unique<ma_sound>();
        ma_result res = ma_sound_init_from_file(
            &m_impl->engine,
            path.c_str(),
            MA_SOUND_FLAG_DECODE,
            nullptr,
            nullptr,
            sound.get()
        );
        if (res != MA_SUCCESS)
        {
            return 0;
        }

        ma_sound_set_spatialization_enabled(sound.get(), MA_TRUE);
        ma_sound_set_position(sound.get(), x, y, z);
        ma_sound_set_min_distance(sound.get(), minDistance);
        ma_sound_set_max_distance(sound.get(), maxDistance);
        ma_sound_set_volume(sound.get(), volume);
        ma_sound_set_pitch(sound.get(), pitch);
        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);

        res = ma_sound_start(sound.get());
        if (res != MA_SUCCESS)
        {
            ma_sound_uninit(sound.get());
            return 0;
        }

        SoundID id = m_impl->nextSoundId++;
        if (id == 0)
        {
            id = m_impl->nextSoundId++;
        }

        m_impl->activeSounds[id] = SoundEntry{ std::move(sound), true };
        return id;
    }

    AudioEngine::SoundID AudioEngine::PlaySound2D(
        const std::string& path,
        float volume,
        float pitch,
        bool loop)
    {
        if (!m_isInitialized || !m_impl || path.empty()) return 0;

        auto sound = std::make_unique<ma_sound>();
        ma_result res = ma_sound_init_from_file(
            &m_impl->engine,
            path.c_str(),
            MA_SOUND_FLAG_DECODE,
            nullptr,
            nullptr,
            sound.get()
        );
        if (res != MA_SUCCESS)
        {
            return 0;
        }

        ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
        ma_sound_set_volume(sound.get(), volume);
        ma_sound_set_pitch(sound.get(), pitch);
        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);

        res = ma_sound_start(sound.get());
        if (res != MA_SUCCESS)
        {
            ma_sound_uninit(sound.get());
            return 0;
        }

        SoundID id = m_impl->nextSoundId++;
        if (id == 0)
        {
            id = m_impl->nextSoundId++;
        }

        m_impl->activeSounds[id] = SoundEntry{ std::move(sound), false };
        return id;
    }

    void AudioEngine::PlaySound(std::string_view filepath, f32 volume)
    {
        PlaySound2D(std::string(filepath), volume, 1.0f, false);
    }

    void AudioEngine::PlaySound3D(std::string_view filepath, f32 x, f32 y, f32 z, f32 volume, f32 minDistance, f32 maxDistance)
    {
        PlaySpatialSound(std::string(filepath), x, y, z, volume, 1.0f, minDistance, maxDistance, false);
    }

    void AudioEngine::StopSound(SoundID id) noexcept
    {
        if (!m_isInitialized || !m_impl || id == 0) return;

        auto it = m_impl->activeSounds.find(id);
        if (it != m_impl->activeSounds.end())
        {
            if (it->second.sound)
            {
                ma_sound_stop(it->second.sound.get());
                ma_sound_uninit(it->second.sound.get());
            }
            m_impl->activeSounds.erase(it);
        }
    }

    bool AudioEngine::IsSoundPlaying(SoundID id) noexcept
    {
        if (!m_isInitialized || !m_impl || id == 0) return false;

        auto it = m_impl->activeSounds.find(id);
        if (it == m_impl->activeSounds.end()) return false;

        auto* snd = it->second.sound.get();
        if (!snd) return false;

        if (ma_sound_at_end(snd)) return false;
        return ma_sound_is_playing(snd) != 0;
    }

    void AudioEngine::SetSoundPosition(SoundID id, float x, float y, float z) noexcept
    {
        if (!m_isInitialized || !m_impl || id == 0) return;

        auto it = m_impl->activeSounds.find(id);
        if (it != m_impl->activeSounds.end() && it->second.sound)
        {
            ma_sound_set_position(it->second.sound.get(), x, y, z);
        }
    }

    void AudioEngine::SetSoundVolume(SoundID id, float volume) noexcept
    {
        if (!m_isInitialized || !m_impl || id == 0) return;

        auto it = m_impl->activeSounds.find(id);
        if (it != m_impl->activeSounds.end() && it->second.sound)
        {
            ma_sound_set_volume(it->second.sound.get(), volume);
        }
    }

    void AudioEngine::SetSoundPitch(SoundID id, float pitch) noexcept
    {
        if (!m_isInitialized || !m_impl || id == 0) return;

        auto it = m_impl->activeSounds.find(id);
        if (it != m_impl->activeSounds.end() && it->second.sound)
        {
            ma_sound_set_pitch(it->second.sound.get(), pitch);
        }
    }

    void AudioEngine::StopAllSounds() noexcept
    {
        if (!m_isInitialized || !m_impl) return;

        for (auto& [id, entry] : m_impl->activeSounds)
        {
            if (entry.sound)
            {
                ma_sound_stop(entry.sound.get());
                ma_sound_uninit(entry.sound.get());
            }
        }
        m_impl->activeSounds.clear();
    }

    void AudioEngine::SetMasterVolume(f32 volume) noexcept
    {
        m_masterVolume = volume;
        if (m_isInitialized && m_impl)
        {
            ma_engine_set_volume(&m_impl->engine, volume);
        }
    }
}
