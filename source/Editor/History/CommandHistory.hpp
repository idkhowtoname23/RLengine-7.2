#pragma once

#include "Core/Types.hpp"
#include "Core/Logger.hpp"
#include "Core/UUID.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <functional>
#include <algorithm>

namespace RL::Editor
{

    class ICommand
    {
    public:
        virtual ~ICommand() = default;

        virtual void Execute() = 0;
        virtual void Undo()    = 0;

        virtual bool TryMerge([[maybe_unused]] const ICommand& other) { return false; }

        [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;
    };

    class CommandHistory
    {
    public:
        static constexpr usize kMaxHistory = 256;

        static void Execute(std::unique_ptr<ICommand> cmd)
        {
            cmd->Execute();

            if (!s_undoStack.empty() && s_undoStack.back()->TryMerge(*cmd))
            {
                return;
            }

            s_undoStack.push_back(std::move(cmd));
            s_redoStack.clear();

            if (s_undoStack.size() > kMaxHistory)
            {
                s_undoStack.erase(s_undoStack.begin());
            }

            RL_LOG_TRACE("CommandHistory: executed '{}' (undo stack: {})",
                s_undoStack.back()->GetName(), s_undoStack.size());
        }

        static void Undo()
        {
            if (s_undoStack.empty()) return;

            auto cmd = std::move(s_undoStack.back());
            s_undoStack.pop_back();
            cmd->Undo();
            s_redoStack.push_back(std::move(cmd));

            RL_LOG_TRACE("CommandHistory: undo '{}' (undo stack: {})",
                s_redoStack.back()->GetName(), s_undoStack.size());
        }

        static void Redo()
        {
            if (s_redoStack.empty()) return;

            auto cmd = std::move(s_redoStack.back());
            s_redoStack.pop_back();
            cmd->Execute();
            s_undoStack.push_back(std::move(cmd));

            RL_LOG_TRACE("CommandHistory: redo '{}' (undo stack: {})",
                s_undoStack.back()->GetName(), s_undoStack.size());
        }

        static void Clear()
        {
            s_undoStack.clear();
            s_redoStack.clear();
        }

        [[nodiscard]] static bool CanUndo() noexcept { return !s_undoStack.empty(); }
        [[nodiscard]] static bool CanRedo() noexcept { return !s_redoStack.empty(); }
        [[nodiscard]] static usize GetUndoCount() noexcept { return s_undoStack.size(); }
        [[nodiscard]] static usize GetRedoCount() noexcept { return s_redoStack.size(); }

        [[nodiscard]] static std::string_view GetUndoName() noexcept
        {
            return s_undoStack.empty() ? "" : s_undoStack.back()->GetName();
        }
        [[nodiscard]] static std::string_view GetRedoName() noexcept
        {
            return s_redoStack.empty() ? "" : s_redoStack.back()->GetName();
        }

    private:
        inline static std::vector<std::unique_ptr<ICommand>> s_undoStack;
        inline static std::vector<std::unique_ptr<ICommand>> s_redoStack;
    };

    struct EntitySnapshot
    {
        Entity OriginalEntity{};
        UUID EntityUUID{};
        std::optional<TagComponent> Tag;
        std::optional<TransformComponent> Transform;
        std::optional<MeshComponent> Mesh;
        std::optional<MaterialComponent> Material;
        std::optional<LightComponent> Light;
        std::optional<CameraComponent> Camera;
        std::optional<RigidBodyComponent> RigidBody;
        std::optional<BoxColliderComponent> BoxCollider;
        std::optional<SphereColliderComponent> SphereCollider;
        std::optional<QuadRenderComponent> QuadRender;
        std::optional<ParticleEmitterComponent> ParticleEmitter;
        std::optional<SkinnedMeshComponent> SkinnedMesh;
        std::optional<AudioSourceComponent> AudioSource;
        std::optional<AudioListenerComponent> AudioListener;
        std::optional<ScriptComponent> Script;

        Entity ParentEntity{};
        std::vector<EntitySnapshot> ChildrenSnapshots;

        static EntitySnapshot Capture(Scene* scene, Entity ent)
        {
            EntitySnapshot snap;
            if (!scene || !scene->IsValid(ent))
            {
                return snap;
            }

            snap.OriginalEntity = ent;
            if (scene->HasComponent<IDComponent>(ent))
            {
                snap.EntityUUID = scene->GetComponent<IDComponent>(ent).ID;
            }
            if (scene->HasComponent<TagComponent>(ent))
            {
                snap.Tag = scene->GetComponent<TagComponent>(ent);
            }
            if (scene->HasComponent<TransformComponent>(ent))
            {
                snap.Transform = scene->GetComponent<TransformComponent>(ent);
            }
            if (scene->HasComponent<MeshComponent>(ent))
            {
                snap.Mesh = scene->GetComponent<MeshComponent>(ent);
            }
            if (scene->HasComponent<MaterialComponent>(ent))
            {
                snap.Material = scene->GetComponent<MaterialComponent>(ent);
            }
            if (scene->HasComponent<LightComponent>(ent))
            {
                snap.Light = scene->GetComponent<LightComponent>(ent);
            }
            if (scene->HasComponent<CameraComponent>(ent))
            {
                snap.Camera = scene->GetComponent<CameraComponent>(ent);
            }
            if (scene->HasComponent<RigidBodyComponent>(ent))
            {
                snap.RigidBody = scene->GetComponent<RigidBodyComponent>(ent);
            }
            if (scene->HasComponent<BoxColliderComponent>(ent))
            {
                snap.BoxCollider = scene->GetComponent<BoxColliderComponent>(ent);
            }
            if (scene->HasComponent<SphereColliderComponent>(ent))
            {
                snap.SphereCollider = scene->GetComponent<SphereColliderComponent>(ent);
            }
            if (scene->HasComponent<QuadRenderComponent>(ent))
            {
                snap.QuadRender = scene->GetComponent<QuadRenderComponent>(ent);
            }
            if (scene->HasComponent<ParticleEmitterComponent>(ent))
            {
                snap.ParticleEmitter = scene->GetComponent<ParticleEmitterComponent>(ent);
            }
            if (scene->HasComponent<SkinnedMeshComponent>(ent))
            {
                snap.SkinnedMesh = scene->GetComponent<SkinnedMeshComponent>(ent);
            }
            if (scene->HasComponent<AudioSourceComponent>(ent))
            {
                snap.AudioSource = scene->GetComponent<AudioSourceComponent>(ent);
            }
            if (scene->HasComponent<AudioListenerComponent>(ent))
            {
                snap.AudioListener = scene->GetComponent<AudioListenerComponent>(ent);
            }
            if (scene->HasComponent<ScriptComponent>(ent))
            {
                snap.Script = scene->GetComponent<ScriptComponent>(ent);
            }

            snap.ParentEntity = scene->GetParent(ent);

            if (scene->HasComponent<RelationshipComponent>(ent))
            {
                const auto& rel = scene->GetComponent<RelationshipComponent>(ent);
                for (Entity child : rel.Children)
                {
                    if (scene->IsValid(child))
                    {
                        snap.ChildrenSnapshots.push_back(Capture(scene, child));
                    }
                }
            }

            return snap;
        }

        Entity Restore(Scene* scene, Entity parentOverride = Entity{}) const
        {
            if (!scene)
            {
                return Entity{};
            }

            const std::string name = Tag.has_value() ? Tag->Tag : "Entity";
            Entity newEnt = !EntityUUID.IsNil() ? scene->CreateEntityWithUUID(EntityUUID, name) : scene->CreateEntity(name);

            RestoreComponent(scene, newEnt, Tag);
            RestoreComponent(scene, newEnt, Transform);
            RestoreComponent(scene, newEnt, Mesh);
            RestoreComponent(scene, newEnt, Material);
            RestoreComponent(scene, newEnt, Light);
            RestoreComponent(scene, newEnt, Camera);
            RestoreComponent(scene, newEnt, RigidBody);
            RestoreComponent(scene, newEnt, BoxCollider);
            RestoreComponent(scene, newEnt, SphereCollider);
            RestoreComponent(scene, newEnt, QuadRender);
            RestoreComponent(scene, newEnt, ParticleEmitter);
            RestoreComponent(scene, newEnt, SkinnedMesh);
            RestoreComponent(scene, newEnt, AudioSource);
            RestoreComponent(scene, newEnt, AudioListener);
            RestoreComponent(scene, newEnt, Script);

            if (parentOverride.IsValid() && scene->IsValid(parentOverride))
            {
                scene->SetParent(newEnt, parentOverride);
            }
            else if (ParentEntity.IsValid() && scene->IsValid(ParentEntity))
            {
                scene->SetParent(newEnt, ParentEntity);
            }

            for (const auto& childSnap : ChildrenSnapshots)
            {
                childSnap.Restore(scene, newEnt);
            }

            return newEnt;
        }

        void RegenerateUUIDs()
        {
            EntityUUID = UUID::Generate();
            for (auto& child : ChildrenSnapshots)
            {
                child.RegenerateUUIDs();
            }
        }

        Entity Clone(Scene* scene, Entity parentOverride = Entity{}) const
        {
            if (!scene)
            {
                return Entity{};
            }

            EntitySnapshot cloneSnap = *this;
            cloneSnap.RegenerateUUIDs();
            return cloneSnap.Restore(scene, parentOverride);
        }

    private:
        template<typename T>
        static void RestoreComponent(Scene* scene, Entity ent, const std::optional<T>& comp)
        {
            if (!comp.has_value()) return;
            if (scene->HasComponent<T>(ent))
            {
                scene->GetComponent<T>(ent) = *comp;
            }
            else
            {
                scene->AddComponent<T>(ent, *comp);
            }
        }
    };

    class MoveCommand : public ICommand
    {
    public:
        MoveCommand(Scene* scene, Entity ent,
                    DirectX::XMFLOAT3 oldPos, DirectX::XMFLOAT3 newPos)
            : m_scene(scene), m_entity(ent), m_oldPos(oldPos), m_newPos(newPos)
        {}

        void Execute() override { SetPos(m_newPos); }
        void Undo()    override { SetPos(m_oldPos); }

        bool TryMerge(const ICommand& other) override
        {
            if (const auto* mc = dynamic_cast<const MoveCommand*>(&other))
            {
                if (mc->m_entity == m_entity)
                {
                    m_newPos = mc->m_newPos;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::string_view GetName() const noexcept override { return "Move Entity"; }

    private:
        void SetPos(const DirectX::XMFLOAT3& pos)
        {
            if (m_scene && m_scene->IsAlive(m_entity) &&
                m_scene->HasComponent<TransformComponent>(m_entity))
            {
                m_scene->GetComponent<TransformComponent>(m_entity).Position = pos;
            }
        }

        Scene*            m_scene;
        Entity            m_entity;
        DirectX::XMFLOAT3 m_oldPos, m_newPos;
    };

    class TransformCommand : public ICommand
    {
    public:
        TransformCommand(Scene* scene, Entity ent,
                         const TransformComponent& oldTrans, const TransformComponent& newTrans)
            : m_scene(scene), m_entity(ent), m_oldTrans(oldTrans), m_newTrans(newTrans)
        {}

        void Execute() override { SetTransform(m_newTrans); }
        void Undo()    override { SetTransform(m_oldTrans); }

        bool TryMerge(const ICommand& other) override
        {
            if (const auto* tc = dynamic_cast<const TransformCommand*>(&other))
            {
                if (tc->m_entity == m_entity)
                {
                    m_newTrans = tc->m_newTrans;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::string_view GetName() const noexcept override { return "Transform Entity"; }

    private:
        void SetTransform(const TransformComponent& t)
        {
            if (m_scene && m_scene->IsAlive(m_entity) &&
                m_scene->HasComponent<TransformComponent>(m_entity))
            {
                m_scene->GetComponent<TransformComponent>(m_entity) = t;
            }
        }

        Scene*             m_scene;
        Entity             m_entity;
        TransformComponent m_oldTrans;
        TransformComponent m_newTrans;
    };

    class DeleteEntityCommand : public ICommand
    {
    public:
        DeleteEntityCommand(Scene* scene, Entity ent)
            : m_scene(scene), m_entity(ent)
        {
            if (m_scene && m_scene->IsValid(m_entity))
            {
                m_snapshot = EntitySnapshot::Capture(m_scene, m_entity);
            }
        }

        DeleteEntityCommand(Scene* scene, const EntitySnapshot& snapshot)
            : m_scene(scene), m_snapshot(snapshot), m_entity(snapshot.OriginalEntity)
        {
        }

        void Execute() override
        {
            if (m_scene && m_scene->IsValid(m_entity))
            {
                m_scene->DestroyEntity(m_entity);
                m_entity = Entity{};
            }
        }

        void Undo() override
        {
            if (!m_scene) return;
            m_entity = m_snapshot.Restore(m_scene);
        }

        [[nodiscard]] Entity GetEntity() const noexcept { return m_entity; }
        [[nodiscard]] const EntitySnapshot& GetSnapshot() const noexcept { return m_snapshot; }
        [[nodiscard]] std::string_view GetName() const noexcept override { return "Delete Entity"; }

    private:
        Scene*         m_scene = nullptr;
        Entity         m_entity{};
        EntitySnapshot m_snapshot;
    };

    class CreateEntityCommand : public ICommand
    {
    public:
        CreateEntityCommand(Scene* scene, std::string_view name = "Entity")
            : m_scene(scene), m_name(name)
        {
            m_snapshot.EntityUUID = UUID::Generate();
            m_snapshot.Tag = TagComponent{ std::string(name) };
            m_snapshot.Transform = TransformComponent{};
            m_hasCapturedSnapshot = true;
        }

        CreateEntityCommand(Scene* scene, const EntitySnapshot& snapshot)
            : m_scene(scene), m_snapshot(snapshot), m_hasCapturedSnapshot(true)
        {
            if (m_snapshot.Tag.has_value())
            {
                m_name = m_snapshot.Tag->Tag;
            }
            if (m_snapshot.EntityUUID.IsNil())
            {
                m_snapshot.EntityUUID = UUID::Generate();
            }
        }

        CreateEntityCommand(Scene* scene, std::string_view name, std::function<void(Entity)> configurator)
            : m_scene(scene), m_name(name), m_configurator(std::move(configurator))
        {
        }

        void Execute() override
        {
            if (!m_scene) return;

            if (m_hasCapturedSnapshot)
            {
                m_entity = m_snapshot.Restore(m_scene);
            }
            else if (m_configurator)
            {
                m_entity = m_scene->CreateEntity(m_name);
                m_configurator(m_entity);
                m_snapshot = EntitySnapshot::Capture(m_scene, m_entity);
                m_hasCapturedSnapshot = true;
            }
            else
            {
                m_entity = m_snapshot.Restore(m_scene);
                m_hasCapturedSnapshot = true;
            }
        }

        void Undo() override
        {
            if (m_scene && m_scene->IsValid(m_entity))
            {
                if (!m_hasCapturedSnapshot)
                {
                    m_snapshot = EntitySnapshot::Capture(m_scene, m_entity);
                    m_hasCapturedSnapshot = true;
                }
                m_scene->DestroyEntity(m_entity);
                m_entity = Entity{};
            }
        }

        [[nodiscard]] Entity GetEntity() const noexcept { return m_entity; }
        [[nodiscard]] const EntitySnapshot& GetSnapshot() const noexcept { return m_snapshot; }
        [[nodiscard]] std::string_view GetName() const noexcept override { return "Create Entity"; }

    private:
        Scene*                      m_scene = nullptr;
        std::string                 m_name = "Entity";
        Entity                      m_entity{};
        EntitySnapshot              m_snapshot;
        std::function<void(Entity)> m_configurator = nullptr;
        bool                        m_hasCapturedSnapshot = false;
    };

    class DuplicateEntityCommand : public ICommand
    {
    public:
        DuplicateEntityCommand(Scene* scene, Entity src)
            : m_scene(scene), m_source(src)
        {
            if (m_scene && m_scene->IsValid(m_source))
            {
                m_snapshot = EntitySnapshot::Capture(m_scene, m_source);
                m_snapshot.RegenerateUUIDs();
                if (m_snapshot.Tag.has_value())
                {
                    m_snapshot.Tag->Tag += " (Copy)";
                }
                if (m_snapshot.Transform.has_value())
                {
                    m_snapshot.Transform->Position.x += 1.0f;
                }
            }
        }

        DuplicateEntityCommand(Scene* scene, Entity src, std::string_view cloneName)
            : m_scene(scene), m_source(src)
        {
            if (m_scene && m_scene->IsValid(m_source))
            {
                m_snapshot = EntitySnapshot::Capture(m_scene, m_source);
                m_snapshot.RegenerateUUIDs();
                if (m_snapshot.Tag.has_value())
                {
                    m_snapshot.Tag->Tag = std::string(cloneName);
                }
            }
        }

        DuplicateEntityCommand(Scene* scene, Entity src, std::string_view cloneName, const DirectX::XMFLOAT3& offset)
            : m_scene(scene), m_source(src)
        {
            if (m_scene && m_scene->IsValid(m_source))
            {
                m_snapshot = EntitySnapshot::Capture(m_scene, m_source);
                m_snapshot.RegenerateUUIDs();
                if (m_snapshot.Tag.has_value())
                {
                    m_snapshot.Tag->Tag = std::string(cloneName);
                }
                if (m_snapshot.Transform.has_value())
                {
                    m_snapshot.Transform->Position.x += offset.x;
                    m_snapshot.Transform->Position.y += offset.y;
                    m_snapshot.Transform->Position.z += offset.z;
                }
            }
        }

        void Execute() override
        {
            if (!m_scene) return;
            m_clone = m_snapshot.Restore(m_scene);
        }

        void Undo() override
        {
            if (m_scene && m_scene->IsValid(m_clone))
            {
                m_scene->DestroyEntity(m_clone);
                m_clone = Entity{};
            }
        }

        [[nodiscard]] Entity GetSource() const noexcept { return m_source; }
        [[nodiscard]] Entity GetClone() const noexcept { return m_clone; }
        [[nodiscard]] const EntitySnapshot& GetSnapshot() const noexcept { return m_snapshot; }
        [[nodiscard]] std::string_view GetName() const noexcept override { return "Duplicate Entity"; }

    private:
        Scene*         m_scene = nullptr;
        Entity         m_source{};
        Entity         m_clone{};
        EntitySnapshot m_snapshot;
    };

    class ReparentEntityCommand : public ICommand
    {
    public:
        ReparentEntityCommand(Scene* scene, Entity child, Entity newParent)
            : m_scene(scene), m_child(child), m_newParent(newParent)
        {
            if (m_scene && m_scene->IsValid(m_child))
            {
                m_oldParent = m_scene->GetParent(m_child);
                if (m_scene->HasComponent<IDComponent>(m_child))
                {
                    m_childUUID = m_scene->GetComponent<IDComponent>(m_child).ID;
                }
            }
            if (m_scene && m_scene->IsValid(m_oldParent) && m_scene->HasComponent<IDComponent>(m_oldParent))
            {
                m_oldParentUUID = m_scene->GetComponent<IDComponent>(m_oldParent).ID;
            }
            if (m_scene && m_scene->IsValid(m_newParent) && m_scene->HasComponent<IDComponent>(m_newParent))
            {
                m_newParentUUID = m_scene->GetComponent<IDComponent>(m_newParent).ID;
            }
            m_childID = m_child.GetID();
            m_oldParentID = m_oldParent.GetID();
            m_newParentID = m_newParent.GetID();
        }

        ReparentEntityCommand(Scene* scene, Entity child, Entity oldParent, Entity newParent)
            : m_scene(scene), m_child(child), m_oldParent(oldParent), m_newParent(newParent)
        {
            if (m_scene && m_scene->IsValid(m_child) && m_scene->HasComponent<IDComponent>(m_child))
            {
                m_childUUID = m_scene->GetComponent<IDComponent>(m_child).ID;
            }
            if (m_scene && m_scene->IsValid(m_oldParent) && m_scene->HasComponent<IDComponent>(m_oldParent))
            {
                m_oldParentUUID = m_scene->GetComponent<IDComponent>(m_oldParent).ID;
            }
            if (m_scene && m_scene->IsValid(m_newParent) && m_scene->HasComponent<IDComponent>(m_newParent))
            {
                m_newParentUUID = m_scene->GetComponent<IDComponent>(m_newParent).ID;
            }
            m_childID = m_child.GetID();
            m_oldParentID = m_oldParent.GetID();
            m_newParentID = m_newParent.GetID();
        }

        void Execute() override
        {
            Entity child = ResolveEntity(m_child, m_childUUID);
            Entity newParent = ResolveEntity(m_newParent, m_newParentUUID);
            if (m_scene && m_scene->IsValid(child))
            {
                m_scene->SetParent(child, newParent);
            }
        }

        void Undo() override
        {
            Entity child = ResolveEntity(m_child, m_childUUID);
            Entity oldParent = ResolveEntity(m_oldParent, m_oldParentUUID);
            if (m_scene && m_scene->IsValid(child))
            {
                m_scene->SetParent(child, oldParent);
            }
        }

        [[nodiscard]] Entity GetChild() const noexcept { return m_child; }
        [[nodiscard]] Entity GetOldParent() const noexcept { return m_oldParent; }
        [[nodiscard]] Entity GetNewParent() const noexcept { return m_newParent; }
        [[nodiscard]] u64 GetChildID() const noexcept { return m_childID; }
        [[nodiscard]] u64 GetOldParentID() const noexcept { return m_oldParentID; }
        [[nodiscard]] u64 GetNewParentID() const noexcept { return m_newParentID; }

        [[nodiscard]] std::string_view GetName() const noexcept override { return "Reparent Entity"; }

    private:
        Entity ResolveEntity(Entity ent, const UUID& uuid) const
        {
            if (m_scene && m_scene->IsValid(ent))
            {
                return ent;
            }
            if (!m_scene || uuid.IsNil())
            {
                return Entity{};
            }
            for (Entity e : m_scene->GetAliveEntities())
            {
                if (m_scene->HasComponent<IDComponent>(e) && m_scene->GetComponent<IDComponent>(e).ID == uuid)
                {
                    return e;
                }
            }
            return Entity{};
        }

        Scene*  m_scene = nullptr;
        Entity  m_child{};
        Entity  m_oldParent{};
        Entity  m_newParent{};
        u64     m_childID = Entity::NullID;
        u64     m_oldParentID = Entity::NullID;
        u64     m_newParentID = Entity::NullID;
        UUID    m_childUUID{};
        UUID    m_oldParentUUID{};
        UUID    m_newParentUUID{};
    };

    class PropertyChangeCommand : public ICommand
    {
    public:
        using Setter = std::function<void()>;

        PropertyChangeCommand(std::string_view name, Setter undoSetter, Setter redoSetter)
            : m_name(name), m_undoSetter(std::move(undoSetter)), m_redoSetter(std::move(redoSetter))
        {}

        void Execute() override { m_redoSetter(); }
        void Undo()    override { m_undoSetter(); }

        [[nodiscard]] std::string_view GetName() const noexcept override { return m_name; }

    private:
        std::string m_name;
        Setter      m_undoSetter;
        Setter      m_redoSetter;
    };

}
