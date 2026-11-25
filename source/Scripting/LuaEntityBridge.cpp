#include "Scripting/LuaEntityBridge.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"
#include "Scene/StressTest.hpp"
#include "Graphics/DX12/TextureManager.hpp"
#include "Graphics/DX12/DX12Texture.hpp"
#include "Audio/AudioEngine.hpp"
#include "Audio/AudioSystem.hpp"
#include "Core/Logger.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/Events/CommonEvents.hpp"

#include <lua.hpp>
#include <format>
#include <string>
#include <cstring>

namespace RL
{
    void LuaEntityBridge::Register(lua_State* L, Scene* scene, TextureManager* textureManager)
    {
        s_L = L;
        s_scene = scene;
        s_textureManager = textureManager;

        if (!L) return;

        if (luaL_newmetatable(L, "RL.Entity"))
        {

            lua_newtable(L);

            lua_pushcfunction(L, &LuaEntityBridge::Entity_SetPosition);
            lua_setfield(L, -2, "SetPosition");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_GetPosition);
            lua_setfield(L, -2, "GetPosition");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_SetRotation);
            lua_setfield(L, -2, "SetRotation");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_GetRotation);
            lua_setfield(L, -2, "GetRotation");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_SetScale);
            lua_setfield(L, -2, "SetScale");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_GetScale);
            lua_setfield(L, -2, "GetScale");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_SetColor);
            lua_setfield(L, -2, "SetColor");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_GetColor);
            lua_setfield(L, -2, "GetColor");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_SetTexture);
            lua_setfield(L, -2, "SetTexture");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_GetTextureIndex);
            lua_setfield(L, -2, "GetTextureIndex");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_GetTag);
            lua_setfield(L, -2, "GetTag");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_SetTag);
            lua_setfield(L, -2, "SetTag");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_SetVisible);
            lua_setfield(L, -2, "SetVisible");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_IsVisible);
            lua_setfield(L, -2, "IsVisible");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_PlaySound);
            lua_setfield(L, -2, "PlaySound");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_StopSound);
            lua_setfield(L, -2, "StopSound");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_IsSoundPlaying);
            lua_setfield(L, -2, "IsSoundPlaying");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_SetAudioVolume);
            lua_setfield(L, -2, "SetAudioVolume");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_PlayAnimation);
            lua_setfield(L, -2, "PlayAnimation");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_CrossFadeAnimation);
            lua_setfield(L, -2, "CrossFadeAnimation");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_StopAnimation);
            lua_setfield(L, -2, "StopAnimation");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_SetAnimationSpeed);
            lua_setfield(L, -2, "SetAnimationSpeed");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_SetAnimationTime);
            lua_setfield(L, -2, "SetAnimationTime");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_GetAnimationTime);
            lua_setfield(L, -2, "GetAnimationTime");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_IsAnimationPlaying);
            lua_setfield(L, -2, "IsAnimationPlaying");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_IsValid);
            lua_setfield(L, -2, "IsValid");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_GetID);
            lua_setfield(L, -2, "GetID");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_GetIndex);
            lua_setfield(L, -2, "GetIndex");

            lua_setfield(L, -2, "__index");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_ToString);
            lua_setfield(L, -2, "__tostring");

            lua_pushcfunction(L, &LuaEntityBridge::Entity_Equal);
            lua_setfield(L, -2, "__eq");
        }
        lua_pop(L, 1);

        lua_getglobal(L, "RL");
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setglobal(L, "RL");
        }

        lua_newtable(L);

        lua_pushcfunction(L, &LuaEntityBridge::Scene_CreateEntity);
        lua_setfield(L, -2, "CreateEntity");

        lua_pushcfunction(L, &LuaEntityBridge::Scene_DestroyEntity);
        lua_setfield(L, -2, "DestroyEntity");

        lua_pushcfunction(L, &LuaEntityBridge::Scene_IsValid);
        lua_setfield(L, -2, "IsValid");

        lua_pushcfunction(L, &LuaEntityBridge::Scene_Clear);
        lua_setfield(L, -2, "Clear");

        lua_pushcfunction(L, &LuaEntityBridge::Scene_GetEntityCount);
        lua_setfield(L, -2, "GetEntityCount");

        lua_pushcfunction(L, &LuaEntityBridge::Scene_StressTest);
        lua_setfield(L, -2, "StressTest");

        lua_pushcfunction(L, &LuaEntityBridge::Scene_ClearStressTest);
        lua_setfield(L, -2, "ClearStressTest");

        lua_setfield(L, -2, "Scene");

        lua_newtable(L);

        lua_pushcfunction(L, &LuaEntityBridge::Texture_Load);
        lua_setfield(L, -2, "Load");

        lua_pushcfunction(L, &LuaEntityBridge::Texture_CreateColor);
        lua_setfield(L, -2, "CreateColor");

        lua_setfield(L, -2, "Texture");

        lua_newtable(L);

        lua_pushcfunction(L, &LuaEntityBridge::Audio_Play);
        lua_setfield(L, -2, "Play");

        lua_pushcfunction(L, &LuaEntityBridge::Audio_Play3D);
        lua_setfield(L, -2, "Play3D");

        lua_pushcfunction(L, &LuaEntityBridge::Audio_SetMasterVolume);
        lua_setfield(L, -2, "SetMasterVolume");

        lua_setfield(L, -2, "Audio");

        lua_newtable(L);

        lua_pushcfunction(L, &LuaEntityBridge::Events_OnEntityCreated);
        lua_setfield(L, -2, "OnEntityCreated");

        lua_pushcfunction(L, &LuaEntityBridge::Events_OnEntityDestroyed);
        lua_setfield(L, -2, "OnEntityDestroyed");

        lua_pushcfunction(L, &LuaEntityBridge::Events_OnCollision);
        lua_setfield(L, -2, "OnCollision");

        lua_pushcfunction(L, &LuaEntityBridge::Events_OnTrigger);
        lua_setfield(L, -2, "OnTrigger");

        lua_pushcfunction(L, &LuaEntityBridge::Events_Clear);
        lua_setfield(L, -2, "Clear");

        lua_setfield(L, -2, "Events");

        lua_pop(L, 1);

        if (s_subCreatedToken != 0)
        {
            EventBus::Unsubscribe<EntityCreatedEvent>(s_subCreatedToken);
            s_subCreatedToken = 0;
        }
        if (s_subDestroyedToken != 0)
        {
            EventBus::Unsubscribe<EntityDestroyedEvent>(s_subDestroyedToken);
            s_subDestroyedToken = 0;
        }
        if (s_subCollisionToken != 0)
        {
            EventBus::Unsubscribe<CollisionEvent>(s_subCollisionToken);
            s_subCollisionToken = 0;
        }
        if (s_subTriggerToken != 0)
        {
            EventBus::Unsubscribe<PhysicsTriggerEvent>(s_subTriggerToken);
            s_subTriggerToken = 0;
        }

        ClearEventCallbacks();

        s_subCreatedToken = EventBus::Subscribe<EntityCreatedEvent>([](const EntityCreatedEvent& ev) {
            DispatchEntityCreated(ev);
        });
        s_subDestroyedToken = EventBus::Subscribe<EntityDestroyedEvent>([](const EntityDestroyedEvent& ev) {
            DispatchEntityDestroyed(ev);
        });
        s_subCollisionToken = EventBus::Subscribe<CollisionEvent>([](const CollisionEvent& ev) {
            DispatchCollision(ev);
        });
        s_subTriggerToken = EventBus::Subscribe<PhysicsTriggerEvent>([](const PhysicsTriggerEvent& ev) {
            DispatchTrigger(ev);
        });

        RL_LOG_INFO("[RLScript] Lua Entity OOP Metatable, RL.Scene, RL.Texture and RL.Audio API registered successfully.");
    }

    void LuaEntityBridge::PushEntity(lua_State* L, Entity entity)
    {
        auto* ud = static_cast<LuaEntityUserData*>(lua_newuserdatauv(L, sizeof(LuaEntityUserData), 0));
        ud->entityID = entity.GetID();
        luaL_setmetatable(L, "RL.Entity");
    }

    LuaEntityBridge::LuaEntityUserData* LuaEntityBridge::CheckEntity(lua_State* L, int arg)
    {
        return static_cast<LuaEntityUserData*>(luaL_checkudata(L, arg, "RL.Entity"));
    }

    int LuaEntityBridge::Entity_SetPosition(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const float x = static_cast<float>(luaL_checknumber(L, 2));
        const float y = static_cast<float>(luaL_checknumber(L, 3));
        const float z = static_cast<float>(luaL_optnumber(L, 4, 0.0));

        if (auto* transform = s_scene->TryGetComponent<TransformComponent>(Entity(ud->entityID)))
        {
            transform->Position = { x, y, z };
        }
        return 0;
    }

    int LuaEntityBridge::Entity_GetPosition(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
            return 3;
        }

        if (const auto* transform = s_scene->TryGetComponent<TransformComponent>(Entity(ud->entityID)))
        {
            lua_pushnumber(L, transform->Position.x);
            lua_pushnumber(L, transform->Position.y);
            lua_pushnumber(L, transform->Position.z);
            return 3;
        }

        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        return 3;
    }

    int LuaEntityBridge::Entity_SetRotation(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const float rx = static_cast<float>(luaL_checknumber(L, 2));
        const float ry = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        const float rz = static_cast<float>(luaL_optnumber(L, 4, 0.0));

        if (auto* transform = s_scene->TryGetComponent<TransformComponent>(Entity(ud->entityID)))
        {
            transform->Rotation = { rx, ry, rz };
        }
        return 0;
    }

    int LuaEntityBridge::Entity_GetRotation(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
            return 3;
        }

        if (const auto* transform = s_scene->TryGetComponent<TransformComponent>(Entity(ud->entityID)))
        {
            lua_pushnumber(L, transform->Rotation.x);
            lua_pushnumber(L, transform->Rotation.y);
            lua_pushnumber(L, transform->Rotation.z);
            return 3;
        }

        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        return 3;
    }

    int LuaEntityBridge::Entity_SetScale(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const float sx = static_cast<float>(luaL_checknumber(L, 2));
        const float sy = static_cast<float>(luaL_optnumber(L, 3, sx));
        const float sz = static_cast<float>(luaL_optnumber(L, 4, 1.0));

        if (auto* transform = s_scene->TryGetComponent<TransformComponent>(Entity(ud->entityID)))
        {
            transform->Scale = { sx, sy, sz };
        }
        return 0;
    }

    int LuaEntityBridge::Entity_GetScale(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            lua_pushnumber(L, 1.0);
            lua_pushnumber(L, 1.0);
            lua_pushnumber(L, 1.0);
            return 3;
        }

        if (const auto* transform = s_scene->TryGetComponent<TransformComponent>(Entity(ud->entityID)))
        {
            lua_pushnumber(L, transform->Scale.x);
            lua_pushnumber(L, transform->Scale.y);
            lua_pushnumber(L, transform->Scale.z);
            return 3;
        }

        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);
        return 3;
    }

    int LuaEntityBridge::Entity_SetColor(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const float r = static_cast<float>(luaL_checknumber(L, 2));
        const float g = static_cast<float>(luaL_checknumber(L, 3));
        const float b = static_cast<float>(luaL_checknumber(L, 4));
        const float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));

        if (auto* quad = s_scene->TryGetComponent<QuadRenderComponent>(Entity(ud->entityID)))
        {
            quad->Color = { r, g, b, a };
        }
        if (auto* meshComp = s_scene->TryGetComponent<MeshComponent>(Entity(ud->entityID)))
        {
            meshComp->Color = { r, g, b, a };
        }
        else if (!s_scene->HasComponent<QuadRenderComponent>(Entity(ud->entityID)))
        {
            s_scene->AddComponent<QuadRenderComponent>(Entity(ud->entityID), DirectX::XMFLOAT4(r, g, b, a), true);
        }
        return 0;
    }

    int LuaEntityBridge::Entity_GetColor(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            lua_pushnumber(L, 1.0);
            lua_pushnumber(L, 1.0);
            lua_pushnumber(L, 1.0);
            lua_pushnumber(L, 1.0);
            return 4;
        }

        if (const auto* quad = s_scene->TryGetComponent<QuadRenderComponent>(Entity(ud->entityID)))
        {
            lua_pushnumber(L, quad->Color.x);
            lua_pushnumber(L, quad->Color.y);
            lua_pushnumber(L, quad->Color.z);
            lua_pushnumber(L, quad->Color.w);
            return 4;
        }
        if (const auto* meshComp = s_scene->TryGetComponent<MeshComponent>(Entity(ud->entityID)))
        {
            lua_pushnumber(L, meshComp->Color.x);
            lua_pushnumber(L, meshComp->Color.y);
            lua_pushnumber(L, meshComp->Color.z);
            lua_pushnumber(L, meshComp->Color.w);
            return 4;
        }

        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);
        return 4;
    }

    int LuaEntityBridge::Entity_SetTexture(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        u32 texIdx = 0;
        std::string texPath;

        if (lua_isnumber(L, 2))
        {
            texIdx = static_cast<u32>(lua_tointeger(L, 2));
        }
        else if (lua_isstring(L, 2))
        {
            texPath = lua_tostring(L, 2);
            if (s_textureManager)
            {
                auto tex = s_textureManager->LoadTexture(texPath);
                if (tex)
                {
                    texIdx = tex->GetDescriptorIndex();
                }
            }
        }

        if (auto* meshComp = s_scene->TryGetComponent<MeshComponent>(Entity(ud->entityID)))
        {
            meshComp->TexturePath = texPath;
            meshComp->TextureIndex = texIdx;
        }

        if (auto* quad = s_scene->TryGetComponent<QuadRenderComponent>(Entity(ud->entityID)))
        {
            quad->TextureIndex = texIdx;
            quad->TexturePath = texPath;
        }
        else if (!s_scene->HasComponent<MeshComponent>(Entity(ud->entityID)))
        {
            s_scene->AddComponent<QuadRenderComponent>(
                Entity(ud->entityID),
                DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
                true,
                texIdx,
                texPath
            );
        }
        return 0;
    }

    int LuaEntityBridge::Entity_GetTextureIndex(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            lua_pushinteger(L, 0);
            return 1;
        }

        if (const auto* quad = s_scene->TryGetComponent<QuadRenderComponent>(Entity(ud->entityID)))
        {
            lua_pushinteger(L, static_cast<lua_Integer>(quad->TextureIndex));
            return 1;
        }
        if (const auto* meshComp = s_scene->TryGetComponent<MeshComponent>(Entity(ud->entityID)))
        {
            lua_pushinteger(L, static_cast<lua_Integer>(meshComp->TextureIndex));
            return 1;
        }

        lua_pushinteger(L, 0);
        return 1;
    }

    int LuaEntityBridge::Entity_GetTag(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            lua_pushstring(L, "");
            return 1;
        }

        if (const auto* tag = s_scene->TryGetComponent<TagComponent>(Entity(ud->entityID)))
        {
            lua_pushstring(L, tag->Tag.c_str());
            return 1;
        }

        lua_pushstring(L, "");
        return 1;
    }

    int LuaEntityBridge::Entity_SetTag(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const char* newTag = luaL_checkstring(L, 2);
        if (auto* tag = s_scene->TryGetComponent<TagComponent>(Entity(ud->entityID)))
        {
            tag->Tag = newTag ? newTag : "Entity";
        }
        return 0;
    }

    int LuaEntityBridge::Entity_SetVisible(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const bool visible = lua_toboolean(L, 2) != 0;
        if (auto* quad = s_scene->TryGetComponent<QuadRenderComponent>(Entity(ud->entityID)))
        {
            quad->IsVisible = visible;
        }
        if (auto* meshComp = s_scene->TryGetComponent<MeshComponent>(Entity(ud->entityID)))
        {
            meshComp->IsVisible = visible;
        }
        return 0;
    }

    int LuaEntityBridge::Entity_IsVisible(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            lua_pushboolean(L, 0);
            return 1;
        }

        if (const auto* quad = s_scene->TryGetComponent<QuadRenderComponent>(Entity(ud->entityID)))
        {
            lua_pushboolean(L, quad->IsVisible ? 1 : 0);
            return 1;
        }
        if (const auto* meshComp = s_scene->TryGetComponent<MeshComponent>(Entity(ud->entityID)))
        {
            lua_pushboolean(L, meshComp->IsVisible ? 1 : 0);
            return 1;
        }

        lua_pushboolean(L, 0);
        return 1;
    }

    int LuaEntityBridge::Entity_PlaySound(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const char* path = luaL_optstring(L, 2, "");
        const f32 volume = static_cast<f32>(luaL_optnumber(L, 3, 1.0));
        const bool loop = lua_isboolean(L, 4) ? (lua_toboolean(L, 4) != 0) : false;

        const Entity ent(ud->entityID);
        auto* src = s_scene->TryGetComponent<AudioSourceComponent>(ent);
        if (!src)
        {
            auto& comp = s_scene->AddComponent<AudioSourceComponent>(ent);
            src = &comp;
        }

        if (src)
        {
            if (path && strlen(path) > 0)
            {
                src->SoundPath = path;
            }
            src->Volume = volume;
            src->Loop = loop;
            src->IsPlaying = true;

            if (auto* audioSystem = s_scene->GetAudioSystem())
            {
                audioSystem->StopEntitySound(ent);
            }
        }

        return 0;
    }

    int LuaEntityBridge::Entity_StopSound(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const Entity ent(ud->entityID);
        if (auto* src = s_scene->TryGetComponent<AudioSourceComponent>(ent))
        {
            src->IsPlaying = false;
        }

        if (auto* audioSystem = s_scene->GetAudioSystem())
        {
            audioSystem->StopEntitySound(ent);
        }

        return 0;
    }

    int LuaEntityBridge::Entity_IsSoundPlaying(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            lua_pushboolean(L, 0);
            return 1;
        }

        const Entity ent(ud->entityID);
        const auto* src = s_scene->TryGetComponent<AudioSourceComponent>(ent);
        if (!src)
        {
            lua_pushboolean(L, 0);
            return 1;
        }

        bool playing = src->IsPlaying;
        if (playing)
        {
            if (const auto* audioSystem = s_scene->GetAudioSystem())
            {
                if (audioSystem->HasTrackedSound(ent) && !audioSystem->IsEntitySoundPlaying(ent))
                {
                    playing = false;
                }
            }
        }

        lua_pushboolean(L, playing ? 1 : 0);
        return 1;
    }

    int LuaEntityBridge::Entity_SetAudioVolume(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const f32 volume = static_cast<f32>(luaL_checknumber(L, 2));

        const Entity ent(ud->entityID);
        if (auto* src = s_scene->TryGetComponent<AudioSourceComponent>(ent))
        {
            src->Volume = volume;
        }

        if (auto* audioSystem = s_scene->GetAudioSystem())
        {
            audioSystem->SetEntitySoundVolume(ent, volume);
        }

        return 0;
    }

    int LuaEntityBridge::Entity_PlayAnimation(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const char* animName = luaL_optstring(L, 2, "");
        const bool hasSpeedArg = (lua_gettop(L) >= 3 && !lua_isnil(L, 3));
        const f32 speed = hasSpeedArg ? static_cast<f32>(luaL_checknumber(L, 3)) : 1.0f;

        const Entity ent(ud->entityID);
        auto* skinned = s_scene->TryGetComponent<SkinnedMeshComponent>(ent);
        if (!skinned)
        {
            skinned = &s_scene->AddComponent<SkinnedMeshComponent>(ent);
        }

        if (skinned)
        {
            if (animName && std::strlen(animName) > 0)
            {
                skinned->Play(animName);
            }
            else
            {
                skinned->IsPlaying = true;
            }
            if (hasSpeedArg)
            {
                skinned->AnimationSpeed = speed;
            }
        }

        return 0;
    }

    int LuaEntityBridge::Entity_CrossFadeAnimation(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const char* animName = luaL_optstring(L, 2, "");
        const f32 duration = static_cast<f32>(luaL_optnumber(L, 3, 0.25));
        const bool hasSpeedArg = (lua_gettop(L) >= 4 && !lua_isnil(L, 4));
        const f32 speed = hasSpeedArg ? static_cast<f32>(luaL_checknumber(L, 4)) : 1.0f;

        const Entity ent(ud->entityID);
        auto* skinned = s_scene->TryGetComponent<SkinnedMeshComponent>(ent);
        if (!skinned)
        {
            skinned = &s_scene->AddComponent<SkinnedMeshComponent>(ent);
        }

        if (skinned)
        {
            if (animName && std::strlen(animName) > 0)
            {
                skinned->CrossFade(animName, duration);
            }
            if (hasSpeedArg)
            {
                skinned->AnimationSpeed = speed;
            }
        }

        return 0;
    }

    int LuaEntityBridge::Entity_StopAnimation(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        if (auto* skinned = s_scene->TryGetComponent<SkinnedMeshComponent>(Entity(ud->entityID)))
        {
            skinned->IsPlaying = false;
        }

        return 0;
    }

    int LuaEntityBridge::Entity_SetAnimationSpeed(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const f32 speed = static_cast<f32>(luaL_checknumber(L, 2));
        if (auto* skinned = s_scene->TryGetComponent<SkinnedMeshComponent>(Entity(ud->entityID)))
        {
            skinned->AnimationSpeed = speed;
        }

        return 0;
    }

    int LuaEntityBridge::Entity_SetAnimationTime(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            return 0;
        }

        const f32 time = static_cast<f32>(luaL_checknumber(L, 2));
        if (auto* skinned = s_scene->TryGetComponent<SkinnedMeshComponent>(Entity(ud->entityID)))
        {
            skinned->AnimationTime = time;
        }

        return 0;
    }

    int LuaEntityBridge::Entity_GetAnimationTime(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            lua_pushnumber(L, 0.0);
            return 1;
        }

        const auto* skinned = s_scene->TryGetComponent<SkinnedMeshComponent>(Entity(ud->entityID));
        lua_pushnumber(L, skinned ? static_cast<lua_Number>(skinned->AnimationTime) : 0.0);
        return 1;
    }

    int LuaEntityBridge::Entity_IsAnimationPlaying(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud || !s_scene || !s_scene->IsValid(Entity(ud->entityID)))
        {
            lua_pushboolean(L, 0);
            return 1;
        }

        const auto* skinned = s_scene->TryGetComponent<SkinnedMeshComponent>(Entity(ud->entityID));
        lua_pushboolean(L, (skinned && skinned->IsPlaying) ? 1 : 0);
        return 1;
    }

    int LuaEntityBridge::Entity_IsValid(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        const bool valid = ud && s_scene && s_scene->IsValid(Entity(ud->entityID));
        lua_pushboolean(L, valid ? 1 : 0);
        return 1;
    }

    int LuaEntityBridge::Entity_GetID(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud)
        {
            lua_pushinteger(L, 0);
            return 1;
        }
        lua_pushinteger(L, static_cast<lua_Integer>(ud->entityID));
        return 1;
    }

    int LuaEntityBridge::Entity_GetIndex(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud)
        {
            lua_pushinteger(L, 0);
            return 1;
        }
        const Entity ent(ud->entityID);
        lua_pushinteger(L, static_cast<lua_Integer>(ent.GetIndex()));
        return 1;
    }

    int LuaEntityBridge::Entity_ToString(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        if (!ud)
        {
            lua_pushstring(L, "RL.Entity(null)");
            return 1;
        }

        const Entity ent(ud->entityID);
        std::string tagStr = "Entity";
        if (s_scene && s_scene->IsValid(ent))
        {
            if (const auto* tag = s_scene->TryGetComponent<TagComponent>(ent))
            {
                tagStr = tag->Tag;
            }
        }
        else
        {
            tagStr = "DEAD";
        }

        const auto str = std::format("RL.Entity(Index={}, Version={}, Tag='{}')", ent.GetIndex(), ent.GetVersion(), tagStr);
        lua_pushstring(L, str.c_str());
        return 1;
    }

    int LuaEntityBridge::Entity_Equal(lua_State* L)
    {
        auto* a = CheckEntity(L, 1);
        auto* b = CheckEntity(L, 2);
        lua_pushboolean(L, (a && b && a->entityID == b->entityID) ? 1 : 0);
        return 1;
    }

    int LuaEntityBridge::Scene_CreateEntity(lua_State* L)
    {
        if (!s_scene)
        {
            return luaL_error(L, "RL.Scene.CreateEntity failed: active Scene reference is null");
        }

        const char* name = luaL_optstring(L, 1, "Entity");
        Entity entity = s_scene->CreateEntity(name ? name : "Entity");
        PushEntity(L, entity);
        return 1;
    }

    int LuaEntityBridge::Scene_DestroyEntity(lua_State* L)
    {
        if (!s_scene)
        {
            return 0;
        }

        auto* ud = CheckEntity(L, 1);
        if (ud)
        {
            s_scene->DestroyEntity(Entity(ud->entityID));
        }
        return 0;
    }

    int LuaEntityBridge::Scene_IsValid(lua_State* L)
    {
        auto* ud = CheckEntity(L, 1);
        const bool valid = ud && s_scene && s_scene->IsValid(Entity(ud->entityID));
        lua_pushboolean(L, valid ? 1 : 0);
        return 1;
    }

    int LuaEntityBridge::Scene_Clear([[maybe_unused]] lua_State* L)
    {
        if (s_scene)
        {
            s_scene->Clear();
        }
        return 0;
    }

    int LuaEntityBridge::Scene_GetEntityCount(lua_State* L)
    {
        const lua_Integer count = s_scene ? static_cast<lua_Integer>(s_scene->GetAliveEntityCount()) : 0;
        lua_pushinteger(L, count);
        return 1;
    }

    int LuaEntityBridge::Scene_StressTest(lua_State* L)
    {
        const int count = static_cast<int>(luaL_optinteger(L, 1, 5000));
        if (s_stressTest && s_scene)
        {
            s_stressTest->Spawn(*s_scene, static_cast<u32>(count));
        }
        return 0;
    }

    int LuaEntityBridge::Scene_ClearStressTest(lua_State*)
    {
        if (s_stressTest && s_scene)
        {
            s_stressTest->Clear(*s_scene);
        }
        return 0;
    }

    int LuaEntityBridge::Texture_Load(lua_State* L)
    {
        const char* pathStr = luaL_checkstring(L, 1);
        if (s_textureManager && pathStr)
        {
            auto tex = s_textureManager->LoadTexture(pathStr);
            lua_pushinteger(L, static_cast<lua_Integer>(tex ? tex->GetDescriptorIndex() : 0));
            return 1;
        }
        lua_pushinteger(L, 0);
        return 1;
    }

    int LuaEntityBridge::Texture_CreateColor(lua_State* L)
    {
        const char* name = luaL_checkstring(L, 1);
        const int r = static_cast<int>(luaL_checkinteger(L, 2));
        const int g = static_cast<int>(luaL_checkinteger(L, 3));
        const int b = static_cast<int>(luaL_checkinteger(L, 4));
        const int a = static_cast<int>(luaL_optinteger(L, 5, 255));

        if (s_textureManager && name)
        {
            auto tex = s_textureManager->CreateColorTexture(
                name,
                static_cast<u8>(r),
                static_cast<u8>(g),
                static_cast<u8>(b),
                static_cast<u8>(a)
            );
            lua_pushinteger(L, static_cast<lua_Integer>(tex ? tex->GetDescriptorIndex() : 0));
            return 1;
        }

        lua_pushinteger(L, 0);
        return 1;
    }

    int LuaEntityBridge::Audio_Play(lua_State* L)
    {
        const char* path = luaL_checkstring(L, 1);
        const f32 vol = static_cast<f32>(luaL_optnumber(L, 2, 1.0));
        AudioEngine::Get().PlaySound(path, vol);
        return 0;
    }

    int LuaEntityBridge::Audio_Play3D(lua_State* L)
    {
        const char* path = luaL_checkstring(L, 1);
        const f32 x = static_cast<f32>(luaL_checknumber(L, 2));
        const f32 y = static_cast<f32>(luaL_checknumber(L, 3));
        const f32 z = static_cast<f32>(luaL_checknumber(L, 4));
        const f32 vol = static_cast<f32>(luaL_optnumber(L, 5, 1.0));
        const f32 minDist = static_cast<f32>(luaL_optnumber(L, 6, 1.0));
        const f32 maxDist = static_cast<f32>(luaL_optnumber(L, 7, 50.0));
        AudioEngine::Get().PlaySpatialSound(path, x, y, z, vol, 1.0f, minDist, maxDist, false);
        return 0;
    }

    int LuaEntityBridge::Audio_SetMasterVolume(lua_State* L)
    {
        const f32 vol = static_cast<f32>(luaL_checknumber(L, 1));
        AudioEngine::Get().SetMasterVolume(vol);
        return 0;
    }

    void LuaEntityBridge::Shutdown() noexcept
    {
        if (s_subCreatedToken != 0)
        {
            EventBus::Unsubscribe<EntityCreatedEvent>(s_subCreatedToken);
            s_subCreatedToken = 0;
        }
        if (s_subDestroyedToken != 0)
        {
            EventBus::Unsubscribe<EntityDestroyedEvent>(s_subDestroyedToken);
            s_subDestroyedToken = 0;
        }
        if (s_subCollisionToken != 0)
        {
            EventBus::Unsubscribe<CollisionEvent>(s_subCollisionToken);
            s_subCollisionToken = 0;
        }
        if (s_subTriggerToken != 0)
        {
            EventBus::Unsubscribe<PhysicsTriggerEvent>(s_subTriggerToken);
            s_subTriggerToken = 0;
        }

        ClearEventCallbacks();
        s_L = nullptr;
    }

    void LuaEntityBridge::ClearEventCallbacks() noexcept
    {
        if (s_L)
        {
            for (int ref : s_onCreatedRefs) luaL_unref(s_L, LUA_REGISTRYINDEX, ref);
            for (int ref : s_onDestroyedRefs) luaL_unref(s_L, LUA_REGISTRYINDEX, ref);
            for (int ref : s_onCollisionRefs) luaL_unref(s_L, LUA_REGISTRYINDEX, ref);
            for (int ref : s_onTriggerRefs) luaL_unref(s_L, LUA_REGISTRYINDEX, ref);
        }
        s_onCreatedRefs.clear();
        s_onDestroyedRefs.clear();
        s_onCollisionRefs.clear();
        s_onTriggerRefs.clear();
    }

    int LuaEntityBridge::Events_OnEntityCreated(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_pushvalue(L, 1);
        const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        s_onCreatedRefs.push_back(ref);
        return 0;
    }

    int LuaEntityBridge::Events_OnEntityDestroyed(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_pushvalue(L, 1);
        const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        s_onDestroyedRefs.push_back(ref);
        return 0;
    }

    int LuaEntityBridge::Events_OnCollision(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_pushvalue(L, 1);
        const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        s_onCollisionRefs.push_back(ref);
        return 0;
    }

    int LuaEntityBridge::Events_OnTrigger(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_pushvalue(L, 1);
        const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        s_onTriggerRefs.push_back(ref);
        return 0;
    }

    int LuaEntityBridge::Events_Clear(lua_State* L)
    {
        ClearEventCallbacks();
        return 0;
    }

    void LuaEntityBridge::DispatchEntityCreated(const EntityCreatedEvent& ev)
    {
        if (!s_L || s_onCreatedRefs.empty()) return;
        if (s_dispatchDepth >= 8) return;
        struct DepthGuard
        {
            DepthGuard() noexcept { s_dispatchDepth++; }
            ~DepthGuard() noexcept { s_dispatchDepth--; }
        } guard;

        const auto callbacks = s_onCreatedRefs;
        for (int ref : callbacks)
        {
            if (s_onCreatedRefs.empty()) break;
            lua_rawgeti(s_L, LUA_REGISTRYINDEX, ref);
            if (!lua_isfunction(s_L, -1))
            {
                lua_pop(s_L, 1);
                continue;
            }
            PushEntity(s_L, ev.entity);
            lua_pushinteger(s_L, static_cast<lua_Integer>(ev.entityID));
            if (lua_pcall(s_L, 2, 0, 0) != LUA_OK)
            {
                const char* err = lua_tostring(s_L, -1);
                RL_LOG_ERROR("[RLScript Event Error] OnEntityCreated: {}", err ? err : "unknown");
                lua_pop(s_L, 1);
            }
        }
    }

    void LuaEntityBridge::DispatchEntityDestroyed(const EntityDestroyedEvent& ev)
    {
        if (!s_L || s_onDestroyedRefs.empty()) return;
        if (s_dispatchDepth >= 8) return;
        struct DepthGuard
        {
            DepthGuard() noexcept { s_dispatchDepth++; }
            ~DepthGuard() noexcept { s_dispatchDepth--; }
        } guard;

        const auto callbacks = s_onDestroyedRefs;
        for (int ref : callbacks)
        {
            if (s_onDestroyedRefs.empty()) break;
            lua_rawgeti(s_L, LUA_REGISTRYINDEX, ref);
            if (!lua_isfunction(s_L, -1))
            {
                lua_pop(s_L, 1);
                continue;
            }
            PushEntity(s_L, ev.entity);
            lua_pushinteger(s_L, static_cast<lua_Integer>(ev.entityID));
            if (lua_pcall(s_L, 2, 0, 0) != LUA_OK)
            {
                const char* err = lua_tostring(s_L, -1);
                RL_LOG_ERROR("[RLScript Event Error] OnEntityDestroyed: {}", err ? err : "unknown");
                lua_pop(s_L, 1);
            }
        }
    }

    void LuaEntityBridge::DispatchCollision(const CollisionEvent& ev)
    {
        if (!s_L || s_onCollisionRefs.empty()) return;
        if (s_dispatchDepth >= 8) return;
        struct DepthGuard
        {
            DepthGuard() noexcept { s_dispatchDepth++; }
            ~DepthGuard() noexcept { s_dispatchDepth--; }
        } guard;

        const auto callbacks = s_onCollisionRefs;
        for (int ref : callbacks)
        {
            if (s_onCollisionRefs.empty()) break;
            lua_rawgeti(s_L, LUA_REGISTRYINDEX, ref);
            if (!lua_isfunction(s_L, -1))
            {
                lua_pop(s_L, 1);
                continue;
            }
            PushEntity(s_L, ev.entityA);
            PushEntity(s_L, ev.entityB);
            lua_pushnumber(s_L, static_cast<lua_Number>(ev.impulse));
            lua_pushnumber(s_L, static_cast<lua_Number>(ev.normal.x));
            lua_pushnumber(s_L, static_cast<lua_Number>(ev.normal.y));
            lua_pushnumber(s_L, static_cast<lua_Number>(ev.normal.z));
            lua_pushnumber(s_L, static_cast<lua_Number>(ev.point.x));
            lua_pushnumber(s_L, static_cast<lua_Number>(ev.point.y));
            lua_pushnumber(s_L, static_cast<lua_Number>(ev.point.z));
            if (lua_pcall(s_L, 9, 0, 0) != LUA_OK)
            {
                const char* err = lua_tostring(s_L, -1);
                RL_LOG_ERROR("[RLScript Event Error] OnCollision: {}", err ? err : "unknown");
                lua_pop(s_L, 1);
            }
        }
    }

    void LuaEntityBridge::DispatchTrigger(const PhysicsTriggerEvent& ev)
    {
        if (!s_L || s_onTriggerRefs.empty()) return;
        if (s_dispatchDepth >= 8) return;
        struct DepthGuard
        {
            DepthGuard() noexcept { s_dispatchDepth++; }
            ~DepthGuard() noexcept { s_dispatchDepth--; }
        } guard;

        const auto callbacks = s_onTriggerRefs;
        for (int ref : callbacks)
        {
            if (s_onTriggerRefs.empty()) break;
            lua_rawgeti(s_L, LUA_REGISTRYINDEX, ref);
            if (!lua_isfunction(s_L, -1))
            {
                lua_pop(s_L, 1);
                continue;
            }
            PushEntity(s_L, ev.triggerEntity);
            PushEntity(s_L, ev.otherEntity);
            lua_pushinteger(s_L, static_cast<lua_Integer>(ev.state));
            const char* stateStr = (ev.state == TriggerEvent::State::Enter) ? "enter" :
                                   ((ev.state == TriggerEvent::State::Stay) ? "stay" : "exit");
            lua_pushstring(s_L, stateStr);
            if (lua_pcall(s_L, 4, 0, 0) != LUA_OK)
            {
                const char* err = lua_tostring(s_L, -1);
                RL_LOG_ERROR("[RLScript Event Error] OnTrigger: {}", err ? err : "unknown");
                lua_pop(s_L, 1);
            }
        }
    }
}
