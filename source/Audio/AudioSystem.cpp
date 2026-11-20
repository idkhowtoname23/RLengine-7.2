#include "Audio/AudioSystem.hpp"
#include "Audio/AudioEngine.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"

#include <unordered_set>

namespace RL
{
    AudioSystem::~AudioSystem()
    {
        for (auto& [entityIdx, soundId] : m_entitySounds)
        {
            AudioEngine::Get().StopSound(soundId);
        }
        m_entitySounds.clear();
    }

    void AudioSystem::Update(Scene& scene, f32)
    {
        Update(scene);
    }

    void AudioSystem::Update(Scene& scene)
    {
        AudioEngine::Get().Update();

        bool listenerFound = false;
        const auto* listenerPool = scene.GetPool<AudioListenerComponent>();
        if (listenerPool)
        {
            const auto entities = listenerPool->GetEntities();
            for (usize i = 0; i < entities.size(); ++i)
            {
                Entity ent(entities[i], 1);
                const auto* listener = scene.TryGetComponent<AudioListenerComponent>(ent);
                if (listener && listener->IsActive)
                {
                    const auto* tr = scene.TryGetComponent<TransformComponent>(ent);
                    if (tr)
                    {
                        const auto R = DirectX::XMMatrixRotationRollPitchYaw(tr->Rotation.x, tr->Rotation.y, tr->Rotation.z);
                        DirectX::XMFLOAT3 fwd;
                        DirectX::XMFLOAT3 up;
                        DirectX::XMStoreFloat3(&fwd, DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), R));
                        DirectX::XMStoreFloat3(&up, DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), R));
                        AudioEngine::Get().SetListenerPosition(tr->Position.x, tr->Position.y, tr->Position.z);
                        AudioEngine::Get().SetListenerDirection(fwd.x, fwd.y, fwd.z, up.x, up.y, up.z);
                        listenerFound = true;
                        break;
                    }
                }
            }
        }

        if (!listenerFound)
        {
            const auto& cam = scene.GetCamera();
            const auto camPos = cam.GetPosition();
            const auto camFwd = cam.GetForward();
            const auto camUp = cam.GetUp();
            AudioEngine::Get().SetListenerPosition(camPos.x, camPos.y, camPos.z);
            AudioEngine::Get().SetListenerDirection(camFwd.x, camFwd.y, camFwd.z, camUp.x, camUp.y, camUp.z);
        }

        const auto* audioPool = scene.GetPool<AudioSourceComponent>();
        if (!audioPool)
        {
            for (auto& [entIdx, soundId] : m_entitySounds)
            {
                AudioEngine::Get().StopSound(soundId);
            }
            m_entitySounds.clear();
            return;
        }

        const auto entities = audioPool->GetEntities();
        std::unordered_set<u32> activeEntityIndices;

        for (usize i = 0; i < entities.size(); ++i)
        {
            const u32 entityIdx = entities[i];
            Entity entity(entityIdx, 1);
            auto* src = scene.TryGetComponent<AudioSourceComponent>(entity);
            if (!src) continue;

            activeEntityIndices.insert(entityIdx);

            if (src->PlayOnStart && !src->IsPlaying && !src->SoundPath.empty())
            {
                src->IsPlaying = true;
                src->PlayOnStart = false;
            }

            auto it = m_entitySounds.find(entityIdx);
            bool soundIsActive = (it != m_entitySounds.end()) && AudioEngine::Get().IsSoundPlaying(it->second);

            if (src->IsPlaying && !src->SoundPath.empty())
            {
                const auto* tr = scene.TryGetComponent<TransformComponent>(entity);
                const auto pos = tr ? tr->Position : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };

                if (!soundIsActive)
                {
                    if (it != m_entitySounds.end())
                    {
                        if (!AudioEngine::Get().IsSoundPlaying(it->second))
                        {
                            m_entitySounds.erase(it);
                            it = m_entitySounds.end();
                            src->IsPlaying = false;
                            continue;
                        }
                        AudioEngine::Get().StopSound(it->second);
                        m_entitySounds.erase(it);
                    }

                    u32 soundId = 0;
                    if (src->Is3D)
                    {
                        soundId = AudioEngine::Get().PlaySpatialSound(
                            src->SoundPath,
                            pos.x, pos.y, pos.z,
                            src->Volume,
                            src->Pitch,
                            src->MinDistance,
                            src->MaxDistance,
                            src->Loop
                        );
                    }
                    else
                    {
                        soundId = AudioEngine::Get().PlaySound2D(
                            src->SoundPath,
                            src->Volume,
                            src->Pitch,
                            src->Loop
                        );
                    }

                    if (soundId != 0)
                    {
                        m_entitySounds[entityIdx] = soundId;
                    }
                    else
                    {
                        src->IsPlaying = false;
                    }
                }
                else
                {
                    u32 soundId = it->second;
                    if (src->Is3D)
                    {
                        AudioEngine::Get().SetSoundPosition(soundId, pos.x, pos.y, pos.z);
                    }
                    AudioEngine::Get().SetSoundVolume(soundId, src->Volume);
                    AudioEngine::Get().SetSoundPitch(soundId, src->Pitch);
                }
            }
            else
            {
                if (it != m_entitySounds.end())
                {
                    AudioEngine::Get().StopSound(it->second);
                    m_entitySounds.erase(it);
                }
                src->IsPlaying = false;
            }
        }

        for (auto it = m_entitySounds.begin(); it != m_entitySounds.end(); )
        {
            if (!activeEntityIndices.contains(it->first))
            {
                AudioEngine::Get().StopSound(it->second);
                it = m_entitySounds.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void AudioSystem::StopEntitySound(Entity entity) noexcept
    {
        const u32 entityIdx = entity.GetIndex();
        auto it = m_entitySounds.find(entityIdx);
        if (it != m_entitySounds.end())
        {
            AudioEngine::Get().StopSound(it->second);
            m_entitySounds.erase(it);
        }
    }

    bool AudioSystem::IsEntitySoundPlaying(Entity entity) const noexcept
    {
        const u32 entityIdx = entity.GetIndex();
        auto it = m_entitySounds.find(entityIdx);
        if (it != m_entitySounds.end())
        {
            return AudioEngine::Get().IsSoundPlaying(it->second);
        }
        return false;
    }

    bool AudioSystem::HasTrackedSound(Entity entity) const noexcept
    {
        const u32 entityIdx = entity.GetIndex();
        return m_entitySounds.contains(entityIdx);
    }

    void AudioSystem::SetEntitySoundVolume(Entity entity, float volume) noexcept
    {
        const u32 entityIdx = entity.GetIndex();
        auto it = m_entitySounds.find(entityIdx);
        if (it != m_entitySounds.end())
        {
            AudioEngine::Get().SetSoundVolume(it->second, volume);
        }
    }
}
