#include "Scripting/ScriptEngine.hpp"
#include "Scripting/LuaEntityBridge.hpp"
#include "Scripting/LuaInputBridge.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"
#include "Platform/FileSystem/VFS.hpp"
#include "Core/Memory/MemoryStats.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/Events/CommonEvents.hpp"

#include <lua.hpp>
#include <format>
#include <vector>

namespace RL
{
    ScriptEngine::ScriptEngine(ScriptEngine&& other) noexcept
        : m_L(other.m_L)
        , m_scene(other.m_scene)
        , m_textureManager(other.m_textureManager)
        , m_window(other.m_window)
        , m_scriptPath(std::move(other.m_scriptPath))
        , m_lastWriteTime(other.m_lastWriteTime)
        , m_reloadCheckTimer(other.m_reloadCheckTimer)
        , m_trackedScriptMemory(other.m_trackedScriptMemory)
        , m_entityScripts(std::move(other.m_entityScripts))
        , m_collisionSubToken(other.m_collisionSubToken)
        , m_triggerSubToken(other.m_triggerSubToken)
    {
        other.m_L = nullptr;
        other.m_scene = nullptr;
        other.m_textureManager = nullptr;
        other.m_window = nullptr;
        other.m_trackedScriptMemory = 0;
        other.m_collisionSubToken = 0;
        other.m_triggerSubToken = 0;
        if (s_activeInstance == &other)
        {
            s_activeInstance = this;
        }
    }

    ScriptEngine& ScriptEngine::operator=(ScriptEngine&& other) noexcept
    {
        if (this != &other)
        {
            Shutdown();
            m_L = other.m_L;
            m_scene = other.m_scene;
            m_textureManager = other.m_textureManager;
            m_window = other.m_window;
            m_scriptPath = std::move(other.m_scriptPath);
            m_lastWriteTime = other.m_lastWriteTime;
            m_reloadCheckTimer = other.m_reloadCheckTimer;
            m_trackedScriptMemory = other.m_trackedScriptMemory;
            m_entityScripts = std::move(other.m_entityScripts);
            m_collisionSubToken = other.m_collisionSubToken;
            m_triggerSubToken = other.m_triggerSubToken;
            other.m_L = nullptr;
            other.m_scene = nullptr;
            other.m_textureManager = nullptr;
            other.m_window = nullptr;
            other.m_trackedScriptMemory = 0;
            other.m_collisionSubToken = 0;
            other.m_triggerSubToken = 0;
            if (s_activeInstance == &other)
            {
                s_activeInstance = this;
            }
        }
        return *this;
    }

    ScriptEngine::~ScriptEngine()
    {
        Shutdown();
    }

    void ScriptEngine::SetScene(Scene* scene) noexcept
    {
        m_scene = scene;
        LuaEntityBridge::SetScene(scene);
        LuaInputBridge::SetContext(scene, m_window);
    }

    void ScriptEngine::SetTextureManager(TextureManager* textureManager) noexcept
    {
        m_textureManager = textureManager;
        LuaEntityBridge::SetTextureManager(textureManager);
    }

    std::expected<void, std::string> ScriptEngine::Initialize(
        const std::filesystem::path& mainScriptPath,
        Scene* scene,
        TextureManager* textureManager,
        Window* window)
    {
        if (m_L)
        {
            Shutdown();
        }

        RL_LOG_INFO("[RLScript] Initializing Lua 5.4 scripting environment...");

        s_activeInstance = this;
        m_scene = scene;
        m_textureManager = textureManager;
        m_window = window;

        m_L = luaL_newstate();
        if (!m_L)
        {
            return std::unexpected("Failed to create Lua state (luaL_newstate returned null).");
        }

        static const luaL_Reg safeLibs[] = {
            { "_G", luaopen_base },
            { LUA_TABLIBNAME, luaopen_table },
            { LUA_STRLIBNAME, luaopen_string },
            { LUA_MATHLIBNAME, luaopen_math },
            { LUA_UTF8LIBNAME, luaopen_utf8 },
            { LUA_COLIBNAME, luaopen_coroutine },
            { nullptr, nullptr }
        };

        for (const auto* lib = safeLibs; lib->func; ++lib)
        {
            luaL_requiref(m_L, lib->name, lib->func, 1);
            lua_pop(m_L, 1);
        }

        lua_pushnil(m_L);
        lua_setglobal(m_L, "dofile");
        lua_pushnil(m_L);
        lua_setglobal(m_L, "loadfile");
        lua_pushnil(m_L);
        lua_setglobal(m_L, "require");
        lua_pushnil(m_L);
        lua_setglobal(m_L, "package");
        lua_pushnil(m_L);
        lua_setglobal(m_L, "io");
        lua_pushnil(m_L);
        lua_setglobal(m_L, "os");
        lua_pushnil(m_L);
        lua_setglobal(m_L, "debug");

        lua_getfield(m_L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
        if (lua_istable(m_L, -1))
        {
            lua_pushnil(m_L); lua_setfield(m_L, -2, "io");
            lua_pushnil(m_L); lua_setfield(m_L, -2, "os");
            lua_pushnil(m_L); lua_setfield(m_L, -2, "package");
            lua_pushnil(m_L); lua_setfield(m_L, -2, "debug");
        }
        lua_pop(m_L, 1);

        lua_pushcfunction(m_L, &ScriptEngine::Lua_Log);
        lua_setglobal(m_L, "print");

        luaL_dostring(m_L, "local _load = load; load = function(c, n, m, e) return _load(c, n, 't', e) end");

        RegisterEngineBindings();

        m_collisionSubToken = EventBus::Subscribe<CollisionEvent>([](const CollisionEvent& ev) {
            if (s_activeInstance)
            {
                s_activeInstance->DispatchEntityCollision(ev.entityA, ev.entityB);
            }
        });
        m_triggerSubToken = EventBus::Subscribe<PhysicsTriggerEvent>([](const PhysicsTriggerEvent& ev) {
            if (s_activeInstance && ev.state == TriggerEvent::State::Enter)
            {
                s_activeInstance->DispatchEntityTriggerEnter(ev.triggerEntity, ev.otherEntity);
            }
        });

        m_scriptPath = mainScriptPath;

        if (!std::filesystem::exists(m_scriptPath))
        {
            return std::unexpected(std::format("Main Lua script not found: '{}'", m_scriptPath.string()));
        }

        std::error_code ec;
        m_lastWriteTime = std::filesystem::last_write_time(m_scriptPath, ec);

        if (!ReloadScript())
        {
            return std::unexpected(std::format("Failed to execute initial Lua script '{}'.", m_scriptPath.string()));
        }

        RL_LOG_INFO("[RLScript] Scripting environment initialized successfully. File: '{}'", m_scriptPath.string());

        const usize currentScriptMemory = static_cast<usize>(lua_gc(m_L, LUA_GCCOUNT, 0)) * 1024;
        if (currentScriptMemory > m_trackedScriptMemory)
        {
            Memory::MemoryStats::Track(Memory::MemoryCategory::Scripts, currentScriptMemory - m_trackedScriptMemory);
        }
        else if (currentScriptMemory < m_trackedScriptMemory)
        {
            Memory::MemoryStats::Untrack(Memory::MemoryCategory::Scripts, m_trackedScriptMemory - currentScriptMemory);
        }
        m_trackedScriptMemory = currentScriptMemory;

        return {};
    }

    void ScriptEngine::Shutdown() noexcept
    {
        if (m_collisionSubToken != 0)
        {
            EventBus::Unsubscribe<CollisionEvent>(m_collisionSubToken);
            m_collisionSubToken = 0;
        }
        if (m_triggerSubToken != 0)
        {
            EventBus::Unsubscribe<PhysicsTriggerEvent>(m_triggerSubToken);
            m_triggerSubToken = 0;
        }

        StopEntityScripts();

        if (s_activeInstance == this)
        {
            s_activeInstance = nullptr;
        }

        if (m_L)
        {
            LuaEntityBridge::Shutdown();
            if (m_trackedScriptMemory > 0)
            {
                Memory::MemoryStats::Untrack(Memory::MemoryCategory::Scripts, m_trackedScriptMemory);
                m_trackedScriptMemory = 0;
            }
            lua_close(m_L);
            m_L = nullptr;
            RL_LOG_INFO("[RLScript] Lua scripting environment closed.");
        }
    }

    void ScriptEngine::RegisterEngineBindings()
    {

        lua_newtable(m_L);

        lua_pushcfunction(m_L, &ScriptEngine::Lua_Log);
        lua_setfield(m_L, -2, "Log");

        lua_setglobal(m_L, "RL");

        LuaEntityBridge::Register(m_L, m_scene, m_textureManager);

        LuaInputBridge::Register(m_L, m_scene, m_window);

        RL_LOG_INFO("[RLScript] Engine bindings registered into Lua global scope 'RL'.");
    }

    int ScriptEngine::Lua_Log(lua_State* L)
    {
        const char* str = luaL_checkstring(L, 1);
        if (str)
        {
            RL_LOG_INFO("[Lua] {}", str);
        }
        return 0;
    }

    bool ScriptEngine::ReloadScript()
    {
        if (!m_L) return false;

        LuaEntityBridge::ClearEventCallbacks();

        const std::string pathStr = m_scriptPath.string();
        RL_LOG_INFO("[RLScript] Loading script file '{}'...", pathStr);

        const int loadStatus = luaL_loadfile(m_L, pathStr.c_str());
        if (loadStatus != LUA_OK)
        {
            const char* err = lua_tostring(m_L, -1);
            RL_LOG_ERROR("[RLScript Compilation Error] {}", err ? err : "Syntax error");
            lua_pop(m_L, 1);
            return false;
        }

        const int execStatus = lua_pcall(m_L, 0, 0, 0);
        if (execStatus != LUA_OK)
        {
            const char* err = lua_tostring(m_L, -1);
            RL_LOG_ERROR("[RLScript Execution Error] {}", err ? err : "Runtime error");
            lua_pop(m_L, 1);
            return false;
        }

        RL_LOG_INFO("[RLScript] Script '{}' loaded and executed successfully.", pathStr);

        lua_getglobal(m_L, "Init");
        if (lua_isfunction(m_L, -1))
        {
            RL_LOG_INFO("[RLScript] Invoking Lua Init() entrypoint...");
            if (lua_pcall(m_L, 0, 0, 0) != LUA_OK)
            {
                const char* err = lua_tostring(m_L, -1);
                RL_LOG_ERROR("[RLScript Runtime Error in Init()] {}", err ? err : "Unknown error");
                lua_pop(m_L, 1);
                return false;
            }
        }
        else
        {
            lua_pop(m_L, 1);
        }

        EventBus::Publish(ScriptReloadEvent{ pathStr });

        return true;
    }

    void ScriptEngine::Tick(f32 dt)
    {
        if (!m_L) return;

        const usize currentScriptMemory = static_cast<usize>(lua_gc(m_L, LUA_GCCOUNT, 0)) * 1024;
        if (currentScriptMemory > m_trackedScriptMemory)
        {
            Memory::MemoryStats::Track(Memory::MemoryCategory::Scripts, currentScriptMemory - m_trackedScriptMemory);
        }
        else if (currentScriptMemory < m_trackedScriptMemory)
        {
            Memory::MemoryStats::Untrack(Memory::MemoryCategory::Scripts, m_trackedScriptMemory - currentScriptMemory);
        }
        m_trackedScriptMemory = currentScriptMemory;

        m_reloadCheckTimer += dt;
        if (m_reloadCheckTimer >= 0.1f)
        {
            m_reloadCheckTimer = 0.0f;

            std::error_code ec;
            const auto currentWriteTime = std::filesystem::last_write_time(m_scriptPath, ec);
            if (!ec && currentWriteTime != m_lastWriteTime)
            {
                m_lastWriteTime = currentWriteTime;
                RL_LOG_INFO("[RLScript] Detected file modification in '{}'. Triggering Hot-Reload...", m_scriptPath.string());
                ReloadScript();
            }
        }

        lua_getglobal(m_L, "Update");
        if (lua_isfunction(m_L, -1))
        {
            lua_pushnumber(m_L, static_cast<lua_Number>(dt));
            if (lua_pcall(m_L, 1, 0, 0) != LUA_OK)
            {
                const char* err = lua_tostring(m_L, -1);
                RL_LOG_ERROR("[RLScript Runtime Error in Update()] {}", err ? err : "Unknown error");
                lua_pop(m_L, 1);
            }
        }
        else
        {
            lua_pop(m_L, 1);
        }

        if (m_scene != nullptr)
        {
            UpdateEntityScripts(*m_scene, dt);
        }
    }

    bool ScriptEngine::LoadEntityScript(Entity entity, const std::string& scriptPath, EntityScriptState& state)
    {
        if (!m_L) return false;

        if (state.envRef != LUA_NOREF)
        {
            luaL_unref(m_L, LUA_REGISTRYINDEX, state.envRef);
            state.envRef = LUA_NOREF;
        }
        state.started = false;
        state.scriptPath = scriptPath;

        std::filesystem::path finalPath;
        if (const auto resolvedPath = VFS::Resolve(scriptPath))
        {
            finalPath = *resolvedPath;
        }
        else
        {
            std::error_code ec;
            if (std::filesystem::exists(scriptPath, ec))
            {
                finalPath = scriptPath;
            }
            else
            {
                RL_LOG_ERROR("[RLScript] Entity {} script not found via VFS: '{}'", entity.GetID(), scriptPath);
                return false;
            }
        }

        const std::string resolvedStr = finalPath.string();
        if (luaL_loadfile(m_L, resolvedStr.c_str()) != LUA_OK)
        {
            const char* err = lua_tostring(m_L, -1);
            RL_LOG_ERROR("[RLScript Compilation Error] Entity {} ('{}'): {}", entity.GetID(), scriptPath, err ? err : "Syntax error");
            lua_pop(m_L, 1);
            return false;
        }

        lua_newtable(m_L);
        lua_newtable(m_L);
        lua_pushglobaltable(m_L);
        lua_setfield(m_L, -2, "__index");
        lua_setmetatable(m_L, -2);

        lua_pushvalue(m_L, -1);
        lua_setupvalue(m_L, -3, 1);

        lua_pushvalue(m_L, -1);
        state.envRef = luaL_ref(m_L, LUA_REGISTRYINDEX);
        lua_pop(m_L, 1);

        if (lua_pcall(m_L, 0, 0, 0) != LUA_OK)
        {
            const char* err = lua_tostring(m_L, -1);
            RL_LOG_ERROR("[RLScript Execution Error] Entity {} ('{}'): {}", entity.GetID(), scriptPath, err ? err : "Runtime error");
            lua_pop(m_L, 1);
            if (state.envRef != LUA_NOREF)
            {
                luaL_unref(m_L, LUA_REGISTRYINDEX, state.envRef);
                state.envRef = LUA_NOREF;
            }
            return false;
        }

        RL_LOG_INFO("[RLScript] Loaded entity {} script '{}'", entity.GetID(), scriptPath);
        return true;
    }

    void ScriptEngine::CallEntityFunction(Entity entity, EntityScriptState& state, const char* funcName, f32 dt, bool hasDt, Entity otherEntity)
    {
        if (!m_L || state.envRef == LUA_NOREF) return;

        lua_rawgeti(m_L, LUA_REGISTRYINDEX, state.envRef);
        if (!lua_istable(m_L, -1))
        {
            lua_pop(m_L, 1);
            return;
        }

        lua_pushstring(m_L, funcName);
        lua_rawget(m_L, -2);
        lua_remove(m_L, -2);

        if (!lua_isfunction(m_L, -1))
        {
            lua_pop(m_L, 1);
            return;
        }

        LuaEntityBridge::PushEntity(m_L, entity);
        int argCount = 1;
        if (hasDt)
        {
            lua_pushnumber(m_L, static_cast<lua_Number>(dt));
            argCount = 2;
        }
        else if (otherEntity.IsValid())
        {
            LuaEntityBridge::PushEntity(m_L, otherEntity);
            argCount = 2;
        }

        if (lua_pcall(m_L, argCount, 0, 0) != LUA_OK)
        {
            const char* err = lua_tostring(m_L, -1);
            RL_LOG_ERROR("[RLScript Runtime Error] Entity {} {}(): {}", entity.GetID(), funcName, err ? err : "Unknown error");
            lua_pop(m_L, 1);
        }
    }

    void ScriptEngine::StartEntityScripts(Scene& scene)
    {
        if (!m_L) return;

        const auto aliveSpan = scene.GetAliveEntities();
        std::vector<Entity> entities(aliveSpan.begin(), aliveSpan.end());

        for (Entity entity : entities)
        {
            if (!scene.IsValid(entity)) continue;
            const auto* sc = scene.TryGetComponent<ScriptComponent>(entity);
            if (!sc || !sc->Enabled || sc->ScriptPath.empty()) continue;

            auto& state = m_entityScripts[entity.GetID()];
            if (state.envRef == LUA_NOREF || state.scriptPath != sc->ScriptPath)
            {
                if (!LoadEntityScript(entity, sc->ScriptPath, state)) continue;
            }

            if (state.envRef != LUA_NOREF && !state.started)
            {
                state.started = true;
                CallEntityFunction(entity, state, "OnStart", 0.0f, false);
            }
        }
    }

    void ScriptEngine::UpdateEntityScripts(Scene& scene, f32 dt)
    {
        if (!m_L) return;

        const auto aliveSpan = scene.GetAliveEntities();
        std::vector<Entity> entities(aliveSpan.begin(), aliveSpan.end());

        for (Entity entity : entities)
        {
            if (!scene.IsValid(entity)) continue;
            const auto* sc = scene.TryGetComponent<ScriptComponent>(entity);
            if (!sc || !sc->Enabled || sc->ScriptPath.empty()) continue;

            auto& state = m_entityScripts[entity.GetID()];
            if (state.scriptPath != sc->ScriptPath)
            {
                if (!LoadEntityScript(entity, sc->ScriptPath, state)) continue;
            }

            if (state.envRef == LUA_NOREF) continue;

            if (!state.started)
            {
                state.started = true;
                CallEntityFunction(entity, state, "OnStart", 0.0f, false);
            }

            if (scene.IsValid(entity))
            {
                CallEntityFunction(entity, state, "OnUpdate", dt, true);
            }
        }

        for (auto it = m_entityScripts.begin(); it != m_entityScripts.end();)
        {
            const Entity ent(it->first);
            if (!scene.IsValid(ent) || !scene.HasComponent<ScriptComponent>(ent))
            {
                if (it->second.envRef != LUA_NOREF)
                {
                    luaL_unref(m_L, LUA_REGISTRYINDEX, it->second.envRef);
                }
                it = m_entityScripts.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void ScriptEngine::StopEntityScripts()
    {
        if (m_L)
        {
            for (auto& [id, state] : m_entityScripts)
            {
                if (state.envRef != LUA_NOREF)
                {
                    luaL_unref(m_L, LUA_REGISTRYINDEX, state.envRef);
                }
            }
        }
        m_entityScripts.clear();
    }

    void ScriptEngine::ReloadEntityScript(Entity entity, Scene& scene)
    {
        if (!m_L || !scene.IsValid(entity)) return;

        const auto* sc = scene.TryGetComponent<ScriptComponent>(entity);
        if (!sc || sc->ScriptPath.empty()) return;

        auto& state = m_entityScripts[entity.GetID()];
        if (LoadEntityScript(entity, sc->ScriptPath, state))
        {
            if (sc->Enabled)
            {
                state.started = true;
                CallEntityFunction(entity, state, "OnStart", 0.0f, false);
            }
        }
    }

    void ScriptEngine::DispatchEntityCollision(Entity a, Entity b)
    {
        if (!m_L) return;

        if (a.IsValid())
        {
            auto it = m_entityScripts.find(a.GetID());
            if (it != m_entityScripts.end() && it->second.envRef != LUA_NOREF)
            {
                bool enabled = true;
                if (m_scene)
                {
                    const auto* sc = m_scene->TryGetComponent<ScriptComponent>(a);
                    enabled = (sc && sc->Enabled);
                }
                if (enabled)
                {
                    CallEntityFunction(a, it->second, "OnCollision", 0.0f, false, b);
                }
            }
        }

        if (b.IsValid())
        {
            auto it = m_entityScripts.find(b.GetID());
            if (it != m_entityScripts.end() && it->second.envRef != LUA_NOREF)
            {
                bool enabled = true;
                if (m_scene)
                {
                    const auto* sc = m_scene->TryGetComponent<ScriptComponent>(b);
                    enabled = (sc && sc->Enabled);
                }
                if (enabled)
                {
                    CallEntityFunction(b, it->second, "OnCollision", 0.0f, false, a);
                }
            }
        }
    }

    void ScriptEngine::DispatchEntityTriggerEnter(Entity trigger, Entity other)
    {
        if (!m_L || !trigger.IsValid()) return;

        auto it = m_entityScripts.find(trigger.GetID());
        if (it != m_entityScripts.end() && it->second.envRef != LUA_NOREF)
        {
            bool enabled = true;
            if (m_scene)
            {
                const auto* sc = m_scene->TryGetComponent<ScriptComponent>(trigger);
                enabled = (sc && sc->Enabled);
            }
            if (enabled)
            {
                CallEntityFunction(trigger, it->second, "OnTriggerEnter", 0.0f, false, other);
            }
        }
    }

    bool ScriptEngine::ExecuteString(std::string_view code)
    {
        if (!m_L)
        {
            return false;
        }

        const std::string codeStr(code);
        const int status = luaL_dostring(m_L, codeStr.c_str());
        if (status != LUA_OK)
        {
            const char* err = lua_tostring(m_L, -1);
            RL_LOG_ERROR("[Lua] {}", err ? err : "Unknown error");
            lua_pop(m_L, 1);
            return false;
        }

        return true;
    }
}
