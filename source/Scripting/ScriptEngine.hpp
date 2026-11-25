#pragma once

#include "Core/Types.hpp"
#include "Core/Logger.hpp"
#include "Scene/Entity.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <expected>
#include <unordered_map>

struct lua_State;

namespace RL
{
    class Scene;
    class TextureManager;
    class Window;

    class ScriptEngine
    {
    public:
        ScriptEngine() = default;
        ~ScriptEngine();

        ScriptEngine(const ScriptEngine&) = delete;
        ScriptEngine& operator=(const ScriptEngine&) = delete;

        ScriptEngine(ScriptEngine&& other) noexcept;
        ScriptEngine& operator=(ScriptEngine&& other) noexcept;

        void SetScene(Scene* scene) noexcept;
        [[nodiscard]] Scene* GetScene() const noexcept { return m_scene; }

        void SetTextureManager(TextureManager* textureManager) noexcept;
        [[nodiscard]] TextureManager* GetTextureManager() const noexcept { return m_textureManager; }

        void SetWindow(Window* window) noexcept { m_window = window; }
        [[nodiscard]] Window* GetWindow() const noexcept { return m_window; }

        [[nodiscard]] std::expected<void, std::string> Initialize(
            const std::filesystem::path& mainScriptPath,
            Scene* scene = nullptr,
            TextureManager* textureManager = nullptr,
            Window* window = nullptr);
        void Shutdown() noexcept;

        void Tick(f32 dt);
        bool ReloadScript();
        bool ExecuteString(std::string_view code);

        void StartEntityScripts(Scene& scene);
        void UpdateEntityScripts(Scene& scene, f32 dt);
        void StopEntityScripts();
        void ReloadEntityScript(Entity entity, Scene& scene);
        void DispatchEntityCollision(Entity a, Entity b);
        void DispatchEntityTriggerEnter(Entity trigger, Entity other);

        [[nodiscard]] static ScriptEngine* GetActive() noexcept { return s_activeInstance; }

        [[nodiscard]] lua_State* GetLuaState() const noexcept { return m_L; }
        [[nodiscard]] bool IsInitialized() const noexcept { return m_L != nullptr; }
        [[nodiscard]] const std::filesystem::path& GetScriptPath() const noexcept { return m_scriptPath; }

    private:
        struct EntityScriptState
        {
            int envRef = -1;
            std::string scriptPath;
            bool started = false;
        };

        void RegisterEngineBindings();
        bool LoadEntityScript(Entity entity, const std::string& scriptPath, EntityScriptState& state);
        void CallEntityFunction(Entity entity, EntityScriptState& state, const char* funcName, f32 dt, bool hasDt, Entity otherEntity = Entity{});
        static int Lua_Log(lua_State* L);

    private:
        lua_State* m_L = nullptr;
        Scene* m_scene = nullptr;
        TextureManager* m_textureManager = nullptr;
        Window* m_window = nullptr;
        std::filesystem::path m_scriptPath;
        std::filesystem::file_time_type m_lastWriteTime{};
        f32 m_reloadCheckTimer = 0.0f;
        usize m_trackedScriptMemory = 0;
        std::unordered_map<u64, EntityScriptState> m_entityScripts;
        u64 m_collisionSubToken = 0;
        u64 m_triggerSubToken = 0;

        inline static ScriptEngine* s_activeInstance = nullptr;
    };
}
