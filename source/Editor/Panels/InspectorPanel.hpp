#pragma once

#include "Editor/UI/Panel.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Core/Types.hpp"

#include <imgui.h>
#include <optional>
#include <functional>
#include <string>
#include <format>
#include <algorithm>
#include <cstring>

namespace RL::Editor
{
    class InspectorPanel : public Panel
    {
    public:
        explicit InspectorPanel(Scene* scene)
            : Panel("Inspector##INS")
            , m_scene(scene)
        {}

        void SetScene(Scene* scene) noexcept { m_scene = scene; }

        void SetSelected(std::optional<Entity> entity) noexcept
        {
            m_selected = entity;
        }

        void OnRender() override
        {
            if (!m_selected.has_value() || !m_scene)
            {
                ImGui::TextDisabled("Select an entity in the Scene Hierarchy.");
                if (m_scene)
                {
                    ImGui::Spacing();
                    ImGui::Separator();
                    DrawWorldEnvironmentSection();
                }
                return;
            }

            const Entity ent = *m_selected;

            if (!m_scene->IsAlive(ent))
            {
                m_selected = std::nullopt;
                ImGui::TextDisabled("Entity is no longer alive.");
                return;
            }

            std::string name = "Entity";
            if (m_scene->HasComponent<TagComponent>(ent))
                name = m_scene->GetComponent<TagComponent>(ent).Tag;

            char nameBuf[128];
            std::strncpy(nameBuf, name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##ent_name", nameBuf, sizeof(nameBuf)))
            {
                if (m_scene->HasComponent<TagComponent>(ent))
                    m_scene->GetComponent<TagComponent>(ent).Tag = nameBuf;
            }
            ImGui::PopStyleColor();

            ImGui::SameLine(0, 6);
            ImGui::TextDisabled("ID:%llu", ent.GetIndex());

            ImGui::Separator();

            DrawTransformComponent(ent);
            DrawMeshComponent(ent);
            DrawQuadRenderComponent(ent);
            DrawLightComponent(ent);
            DrawMaterialComponent(ent);
            DrawRigidBodyComponent(ent);
            DrawBoxColliderComponent(ent);
            DrawSphereColliderComponent(ent);
            DrawCapsuleColliderComponent(ent);
            DrawDistanceJointComponent(ent);
            DrawHingeJointComponent(ent);
            DrawFixedJointComponent(ent);
            DrawAudioSourceComponent(ent);
            DrawAudioListenerComponent(ent);
            DrawSkinnedMeshComponent(ent);
            DrawScriptComponent(ent);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const f32 btnWidth = ImGui::GetContentRegionAvail().x;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.91f, 0.49f, 0.05f, 0.6f));
            if (ImGui::Button("+ Add Component", { btnWidth, 0 }))
            {
                ImGui::OpenPopup("##add_component_menu");
            }
            ImGui::PopStyleColor(2);

            if (ImGui::BeginPopup("##add_component_menu"))
            {
                if (ImGui::MenuItem("Transform"))
                {
                    if (!m_scene->HasComponent<TransformComponent>(ent))
                        m_scene->AddComponent<TransformComponent>(ent);
                }
                if (ImGui::MenuItem("Mesh Renderer"))
                {
                    if (!m_scene->HasComponent<MeshComponent>(ent))
                        m_scene->AddComponent<MeshComponent>(ent, "Cube");
                }
                if (ImGui::MenuItem("Quad Renderer"))
                {
                    if (!m_scene->HasComponent<QuadRenderComponent>(ent))
                        m_scene->AddComponent<QuadRenderComponent>(ent);
                }
                if (ImGui::MenuItem("Light"))
                {
                    if (!m_scene->HasComponent<LightComponent>(ent))
                        m_scene->AddComponent<LightComponent>(ent);
                }
                if (ImGui::MenuItem("Material"))
                {
                    if (!m_scene->HasComponent<MaterialComponent>(ent))
                        m_scene->AddComponent<MaterialComponent>(ent);
                }
                if (ImGui::MenuItem("RigidBody"))
                {
                    if (!m_scene->HasComponent<RigidBodyComponent>(ent))
                        m_scene->AddComponent<RigidBodyComponent>(ent);
                }
                if (ImGui::MenuItem("Box Collider"))
                {
                    if (!m_scene->HasComponent<BoxColliderComponent>(ent))
                        m_scene->AddComponent<BoxColliderComponent>(ent);
                }
                if (ImGui::MenuItem("Sphere Collider"))
                {
                    if (!m_scene->HasComponent<SphereColliderComponent>(ent))
                        m_scene->AddComponent<SphereColliderComponent>(ent);
                }
                if (ImGui::MenuItem("Capsule Collider"))
                {
                    if (!m_scene->HasComponent<CapsuleColliderComponent>(ent))
                        m_scene->AddComponent<CapsuleColliderComponent>(ent);
                }
                if (ImGui::MenuItem("Distance Joint"))
                {
                    if (!m_scene->HasComponent<DistanceJointComponent>(ent))
                        m_scene->AddComponent<DistanceJointComponent>(ent);
                }
                if (ImGui::MenuItem("Hinge Joint"))
                {
                    if (!m_scene->HasComponent<HingeJointComponent>(ent))
                        m_scene->AddComponent<HingeJointComponent>(ent);
                }
                if (ImGui::MenuItem("Fixed Joint"))
                {
                    if (!m_scene->HasComponent<FixedJointComponent>(ent))
                        m_scene->AddComponent<FixedJointComponent>(ent);
                }
                if (ImGui::MenuItem("Audio Source"))
                {
                    if (!m_scene->HasComponent<AudioSourceComponent>(ent))
                        m_scene->AddComponent<AudioSourceComponent>(ent);
                }
                if (ImGui::MenuItem("Audio Listener"))
                {
                    if (!m_scene->HasComponent<AudioListenerComponent>(ent))
                        m_scene->AddComponent<AudioListenerComponent>(ent);
                }
                if (ImGui::MenuItem("Skinned Mesh"))
                {
                    if (!m_scene->HasComponent<SkinnedMeshComponent>(ent))
                        m_scene->AddComponent<SkinnedMeshComponent>(ent);
                }
                if (ImGui::MenuItem("Script"))
                {
                    if (!m_scene->HasComponent<ScriptComponent>(ent))
                        m_scene->AddComponent<ScriptComponent>(ent);
                }
                ImGui::EndPopup();
            }
        }

    private:

        [[nodiscard]] static bool ComponentHeader(const char* name, bool* pRemove = nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.35f, 0.35f, 0.35f, 1.0f));

            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            const bool open = ImGui::CollapsingHeader(name);

            ImGui::PopStyleColor(3);

            if (pRemove)
            {
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f,0.1f,0.1f,0.6f));
                const std::string removeId = std::format("##rm_{}", name);
                if (ImGui::SmallButton(removeId.c_str())) *pRemove = true;
                ImGui::PopStyleColor(2);
            }

            return open;
        }

        static void LabeledField(const char* label, f32 width = 100.0f)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("%s", label);
            ImGui::PopStyleColor();
            ImGui::SameLine(width);
        }

        void DrawTransformComponent(Entity ent)
        {
            if (!m_scene->HasComponent<TransformComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Transform", &remove)) return;
            if (remove) { m_scene->RemoveComponent<TransformComponent>(ent); return; }

            auto& t = m_scene->GetComponent<TransformComponent>(ent);

            ImGui::PushItemWidth(-1);
            ImGui::Spacing();

            LabeledField("Position");
            float pos[3] = { t.Position.x, t.Position.y, t.Position.z };
            if (ImGui::DragFloat3("##pos", pos, 0.05f))
            {
                t.Position = { pos[0], pos[1], pos[2] };
            }

            LabeledField("Rotation");
            float rot[3] = { t.Rotation.x, t.Rotation.y, t.Rotation.z };
            if (ImGui::DragFloat3("##rot", rot, 0.5f))
            {
                t.Rotation = { rot[0], rot[1], rot[2] };
            }

            LabeledField("Scale");
            float scl[3] = { t.Scale.x, t.Scale.y, t.Scale.z };
            if (ImGui::DragFloat3("##scl", scl, 0.01f, 0.001f, 1000.0f))
            {
                t.Scale = { scl[0], scl[1], scl[2] };
            }

            ImGui::PopItemWidth();
            ImGui::Spacing();
        }

        void DrawMeshComponent(Entity ent)
        {
            if (!m_scene->HasComponent<MeshComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Mesh Renderer", &remove)) return;
            if (remove) { m_scene->RemoveComponent<MeshComponent>(ent); return; }

            auto& mc = m_scene->GetComponent<MeshComponent>(ent);

            ImGui::Spacing();
            LabeledField("Mesh", 80.0f);
            char meshBuf[128];
            std::strncpy(meshBuf, mc.MeshName.c_str(), sizeof(meshBuf) - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##mesh_name", meshBuf, sizeof(meshBuf)))
            {
                mc.MeshName = meshBuf;
            }
            ImGui::Spacing();
        }

        void DrawQuadRenderComponent(Entity ent)
        {
            if (!m_scene->HasComponent<QuadRenderComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Quad Renderer", &remove)) return;
            if (remove) { m_scene->RemoveComponent<QuadRenderComponent>(ent); return; }

            auto& qr = m_scene->GetComponent<QuadRenderComponent>(ent);

            ImGui::Spacing();

            LabeledField("Color", 80.0f);
            float col[4] = { qr.Color.x, qr.Color.y, qr.Color.z, qr.Color.w };
            if (ImGui::ColorEdit4("##quad_color", col))
            {
                qr.Color = { col[0], col[1], col[2], col[3] };
            }

            LabeledField("Visible", 80.0f);
            ImGui::Checkbox("##quad_visible", &qr.IsVisible);

            LabeledField("Tex Index", 80.0f);
            int texIdx = static_cast<int>(qr.TextureIndex);
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::DragInt("##tex_idx", &texIdx, 1, 0, 1024))
            {
                qr.TextureIndex = static_cast<u32>(std::max(0, texIdx));
            }

            ImGui::Spacing();
        }

        void DrawTargetEntitySelector(const char* comboId, Entity owner, Entity& target)
        {
            std::string preview = "None";
            if (target.IsValid() && m_scene->IsAlive(target))
            {
                std::string tName = m_scene->HasComponent<TagComponent>(target)
                    ? m_scene->GetComponent<TagComponent>(target).Tag
                    : "Entity";
                preview = std::format("{} (#{})", tName, target.GetIndex());
            }

            if (ImGui::BeginCombo(comboId, preview.c_str()))
            {
                if (ImGui::Selectable("None", !target.IsValid()))
                {
                    target = Entity{};
                }
                for (Entity candidate : m_scene->GetAliveEntities())
                {
                    if (!m_scene->IsAlive(candidate) || candidate == owner) continue;
                    std::string cName = m_scene->HasComponent<TagComponent>(candidate)
                        ? m_scene->GetComponent<TagComponent>(candidate).Tag
                        : "Entity";
                    const std::string itemLabel = std::format("{} (#{})##{}", cName, candidate.GetIndex(), candidate.GetID());
                    const bool isSelected = (target == candidate);
                    if (ImGui::Selectable(itemLabel.c_str(), isSelected))
                    {
                        target = candidate;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        void DrawLightComponent(Entity ent)
        {
            if (!m_scene->HasComponent<LightComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Light", &remove)) return;
            if (remove) { m_scene->RemoveComponent<LightComponent>(ent); return; }

            auto& lc = m_scene->GetComponent<LightComponent>(ent);
            ImGui::Spacing();

            LabeledField("Type", 90.0f);
            const char* types[] = { "Point", "Directional", "Spot" };
            int tIdx = static_cast<int>(lc.Type);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##light_type", &tIdx, types, IM_ARRAYSIZE(types)))
            {
                lc.Type = static_cast<LightType>(tIdx);
            }

            LabeledField("Color", 90.0f);
            float col[3] = { lc.Color.x, lc.Color.y, lc.Color.z };
            ImGui::SetNextItemWidth(-1);
            if (ImGui::ColorEdit3("##light_color", col))
            {
                lc.Color = { col[0], col[1], col[2] };
            }

            LabeledField("Intensity", 90.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##light_intensity", &lc.Intensity, 0.1f, 0.0f, 100.0f);

            LabeledField("Range", 90.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##light_range", &lc.Range, 0.2f, 0.1f, 500.0f);

            if (lc.Type == LightType::Spot)
            {
                LabeledField("Spot Angle", 90.0f);
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##light_spot_angle", &lc.SpotAngle, 1.0f, 179.0f);
            }

            LabeledField("Shadows", 90.0f);
            ImGui::Checkbox("##light_shadows", &lc.CastShadows);
            ImGui::Spacing();
        }

        void DrawMaterialComponent(Entity ent)
        {
            if (!m_scene->HasComponent<MaterialComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Material", &remove)) return;
            if (remove) { m_scene->RemoveComponent<MaterialComponent>(ent); return; }

            auto& mat = m_scene->GetComponent<MaterialComponent>(ent);
            ImGui::Spacing();

            LabeledField("Base Color", 100.0f);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::ColorEdit4("##mat_base", &mat.BaseColor.x))
            {
                mat.AlbedoColor = mat.BaseColor;
            }

            LabeledField("Metallic", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##mat_metallic", &mat.Metallic, 0.0f, 1.0f);

            LabeledField("Roughness", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##mat_roughness", &mat.Roughness, 0.0f, 1.0f);

            LabeledField("AO", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##mat_ao", &mat.AO, 0.0f, 1.0f);

            LabeledField("Normal Str", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##mat_normal", &mat.NormalStrength, 0.0f, 2.0f);

            LabeledField("Emission", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##mat_emission", &mat.EmissionStrength, 0.0f, 20.0f);

            LabeledField("Emissive Col", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::ColorEdit3("##mat_emissive_col", &mat.EmissiveColor.x);

            LabeledField("Emissive Int", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##mat_emissive_int", &mat.EmissiveIntensity, 0.1f, 0.0f, 50.0f);
            ImGui::Spacing();
        }

        void DrawRigidBodyComponent(Entity ent)
        {
            if (!m_scene->HasComponent<RigidBodyComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("RigidBody", &remove)) return;
            if (remove) { m_scene->RemoveComponent<RigidBodyComponent>(ent); return; }

            auto& rb = m_scene->GetComponent<RigidBodyComponent>(ent);
            ImGui::Spacing();

            LabeledField("Body Type", 100.0f);
            const char* bodyTypes[] = { "Static", "Dynamic", "Kinematic" };
            int bType = static_cast<int>(rb.Type);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##rb_type", &bType, bodyTypes, IM_ARRAYSIZE(bodyTypes)))
            {
                rb.Type = static_cast<BodyType>(bType);
            }

            LabeledField("Mass", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##rb_mass", &rb.Mass, 0.1f, 0.001f, 10000.0f);

            LabeledField("Restitution", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##rb_rest", &rb.Restitution, 0.0f, 1.0f);

            LabeledField("Friction", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##rb_fric", &rb.Friction, 0.0f, 1.0f);

            LabeledField("Lin Damping", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##rb_ldamp", &rb.LinearDamping, 0.01f, 0.0f, 10.0f);

            LabeledField("Ang Damping", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##rb_adamp", &rb.AngularDamping, 0.01f, 0.0f, 10.0f);

            LabeledField("Use Gravity", 100.0f);
            ImGui::Checkbox("##rb_grav", &rb.UseGravity);

            LabeledField("Gravity Scale", 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##rb_gscale", &rb.GravityScale, 0.05f, -5.0f, 5.0f);
            ImGui::Spacing();
        }

        void DrawBoxColliderComponent(Entity ent)
        {
            if (!m_scene->HasComponent<BoxColliderComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Box Collider", &remove)) return;
            if (remove) { m_scene->RemoveComponent<BoxColliderComponent>(ent); return; }

            auto& bc = m_scene->GetComponent<BoxColliderComponent>(ent);
            ImGui::Spacing();
            LabeledField("Half Size", 90.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##bc_half", &bc.HalfSize.x, 0.05f, 0.01f, 100.0f);

            LabeledField("Offset", 90.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##bc_offset", &bc.Offset.x, 0.05f);

            LabeledField("Is Trigger", 90.0f);
            ImGui::Checkbox("##bc_trig", &bc.IsTrigger);
            ImGui::Spacing();
        }

        void DrawSphereColliderComponent(Entity ent)
        {
            if (!m_scene->HasComponent<SphereColliderComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Sphere Collider", &remove)) return;
            if (remove) { m_scene->RemoveComponent<SphereColliderComponent>(ent); return; }

            auto& sc = m_scene->GetComponent<SphereColliderComponent>(ent);
            ImGui::Spacing();
            LabeledField("Radius", 90.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##sc_rad", &sc.Radius, 0.05f, 0.01f, 100.0f);

            LabeledField("Offset", 90.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##sc_offset", &sc.Offset.x, 0.05f);

            LabeledField("Is Trigger", 90.0f);
            ImGui::Checkbox("##sc_trig", &sc.IsTrigger);
            ImGui::Spacing();
        }

        void DrawCapsuleColliderComponent(Entity ent)
        {
            if (!m_scene->HasComponent<CapsuleColliderComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Capsule Collider", &remove)) return;
            if (remove) { m_scene->RemoveComponent<CapsuleColliderComponent>(ent); return; }

            auto& cc = m_scene->GetComponent<CapsuleColliderComponent>(ent);
            ImGui::Spacing();
            LabeledField("Radius", 90.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##cc_rad", &cc.Radius, 0.05f, 0.01f, 100.0f);

            LabeledField("Height", 90.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##cc_height", &cc.Height, 0.05f, 0.01f, 100.0f);

            LabeledField("Offset", 90.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##cc_offset", &cc.Offset.x, 0.05f);

            LabeledField("Is Trigger", 90.0f);
            ImGui::Checkbox("##cc_trig", &cc.IsTrigger);
            ImGui::Spacing();
        }

        void DrawDistanceJointComponent(Entity ent)
        {
            if (!m_scene->HasComponent<DistanceJointComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Distance Joint", &remove)) return;
            if (remove) { m_scene->RemoveComponent<DistanceJointComponent>(ent); return; }

            auto& dj = m_scene->GetComponent<DistanceJointComponent>(ent);
            ImGui::Spacing();

            LabeledField("Target", 95.0f);
            ImGui::SetNextItemWidth(-1);
            DrawTargetEntitySelector("##dj_target", ent, dj.TargetEntity);

            LabeledField("Anchor A", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##dj_anchor_a", &dj.AnchorA.x, 0.05f);

            LabeledField("Anchor B", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##dj_anchor_b", &dj.AnchorB.x, 0.05f);

            LabeledField("Auto Dist", 95.0f);
            ImGui::Checkbox("##dj_autodist", &dj.AutoDistance);

            LabeledField("Distance", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##dj_dist", &dj.TargetDistance, 0.05f, 0.0f, 1000.0f);

            LabeledField("Stiffness", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##dj_stiff", &dj.Stiffness, 0.0f, 1.0f);

            LabeledField("Damping", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##dj_damp", &dj.Damping, 0.0f, 1.0f);
            ImGui::Spacing();
        }

        void DrawHingeJointComponent(Entity ent)
        {
            if (!m_scene->HasComponent<HingeJointComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Hinge Joint", &remove)) return;
            if (remove) { m_scene->RemoveComponent<HingeJointComponent>(ent); return; }

            auto& hj = m_scene->GetComponent<HingeJointComponent>(ent);
            ImGui::Spacing();

            LabeledField("Target", 95.0f);
            ImGui::SetNextItemWidth(-1);
            DrawTargetEntitySelector("##hj_target", ent, hj.TargetEntity);

            LabeledField("Anchor", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##hj_anchor", &hj.Anchor.x, 0.05f);

            LabeledField("Axis", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##hj_axis", &hj.Axis.x, 0.05f);

            LabeledField("Limits", 95.0f);
            ImGui::Checkbox("##hj_lim", &hj.EnableLimits);
            if (hj.EnableLimits)
            {
                LabeledField("Lower Lim", 95.0f);
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderAngle("##hj_low", &hj.LowerAngleLimit, -180.0f, 180.0f);

                LabeledField("Upper Lim", 95.0f);
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderAngle("##hj_high", &hj.UpperAngleLimit, -180.0f, 180.0f);
            }

            LabeledField("Motor", 95.0f);
            ImGui::Checkbox("##hj_motor", &hj.EnableMotor);
            if (hj.EnableMotor)
            {
                LabeledField("Motor Spd", 95.0f);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##hj_mspd", &hj.MotorSpeed, 0.1f, -50.0f, 50.0f);

                LabeledField("Max Torque", 95.0f);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##hj_mtorq", &hj.MaxMotorTorque, 0.5f, 0.0f, 1000.0f);
            }
            ImGui::Spacing();
        }

        void DrawFixedJointComponent(Entity ent)
        {
            if (!m_scene->HasComponent<FixedJointComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Fixed Joint", &remove)) return;
            if (remove) { m_scene->RemoveComponent<FixedJointComponent>(ent); return; }

            auto& fj = m_scene->GetComponent<FixedJointComponent>(ent);
            ImGui::Spacing();

            LabeledField("Target", 95.0f);
            ImGui::SetNextItemWidth(-1);
            DrawTargetEntitySelector("##fj_target", ent, fj.TargetEntity);

            LabeledField("Offset", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##fj_off", &fj.RelativeOffset.x, 0.05f);

            LabeledField("Rel Rot", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##fj_rot", &fj.RelativeRotation.x, 0.05f);

            LabeledField("Break Force", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##fj_bf", &fj.BreakForce, 0.5f, 0.0f, 10000.0f);

            LabeledField("Broken", 95.0f);
            ImGui::Checkbox("##fj_brk", &fj.IsBroken);
            ImGui::Spacing();
        }

        void DrawAudioSourceComponent(Entity ent)
        {
            if (!m_scene->HasComponent<AudioSourceComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Audio Source", &remove)) return;
            if (remove) { m_scene->RemoveComponent<AudioSourceComponent>(ent); return; }

            auto& as = m_scene->GetComponent<AudioSourceComponent>(ent);
            ImGui::Spacing();

            LabeledField("Sound Path", 95.0f);
            char pathBuf[256]{};
            std::strncpy(pathBuf, as.SoundPath.c_str(), sizeof(pathBuf) - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##as_path", pathBuf, sizeof(pathBuf)))
            {
                as.SoundPath = pathBuf;
            }

            LabeledField("Volume", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##as_vol", &as.Volume, 0.0f, 2.0f);

            LabeledField("Pitch", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##as_pitch", &as.Pitch, 0.1f, 3.0f);

            LabeledField("Min Dist", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##as_min", &as.MinDistance, 0.1f, 0.01f, 1000.0f);

            LabeledField("Max Dist", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##as_max", &as.MaxDistance, 0.5f, 0.1f, 5000.0f);

            LabeledField("Is 3D", 95.0f);
            ImGui::Checkbox("##as_3d", &as.Is3D);

            LabeledField("Loop", 95.0f);
            ImGui::Checkbox("##as_loop", &as.Loop);

            LabeledField("Play Start", 95.0f);
            ImGui::Checkbox("##as_pos", &as.PlayOnStart);
            ImGui::Spacing();
        }

        void DrawAudioListenerComponent(Entity ent)
        {
            if (!m_scene->HasComponent<AudioListenerComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Audio Listener", &remove)) return;
            if (remove) { m_scene->RemoveComponent<AudioListenerComponent>(ent); return; }

            auto& al = m_scene->GetComponent<AudioListenerComponent>(ent);
            ImGui::Spacing();
            LabeledField("Active", 95.0f);
            ImGui::Checkbox("##al_active", &al.IsActive);
            ImGui::Spacing();
        }

        void DrawSkinnedMeshComponent(Entity ent)
        {
            if (!m_scene->HasComponent<SkinnedMeshComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Skinned Mesh", &remove)) return;
            if (remove) { m_scene->RemoveComponent<SkinnedMeshComponent>(ent); return; }

            auto& sk = m_scene->GetComponent<SkinnedMeshComponent>(ent);
            ImGui::Spacing();

            LabeledField("Model Path", 95.0f);
            char modelBuf[256]{};
            std::strncpy(modelBuf, sk.ModelPath.c_str(), sizeof(modelBuf) - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##sk_model", modelBuf, sizeof(modelBuf)))
            {
                sk.ModelPath = modelBuf;
            }

            LabeledField("Skeleton", 95.0f);
            char skelBuf[128]{};
            std::strncpy(skelBuf, sk.SkeletonName.c_str(), sizeof(skelBuf) - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##sk_skel", skelBuf, sizeof(skelBuf)))
            {
                sk.SkeletonName = skelBuf;
            }

            LabeledField("Animation", 95.0f);
            char animBuf[128]{};
            std::strncpy(animBuf, sk.CurrentAnimation.c_str(), sizeof(animBuf) - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##sk_anim", animBuf, sizeof(animBuf)))
            {
                sk.CurrentAnimation = animBuf;
            }

            LabeledField("Timeline", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##sk_time", &sk.AnimationTime, 0.0f, 10.0f, "%.2f s");

            LabeledField("Speed", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##sk_speed", &sk.AnimationSpeed, 0.05f, 0.0f, 10.0f);

            LabeledField("Loop", 95.0f);
            ImGui::Checkbox("##sk_loop", &sk.Loop);

            LabeledField("Playing", 95.0f);
            ImGui::Checkbox("##sk_playing", &sk.IsPlaying);

            LabeledField("Visible", 95.0f);
            ImGui::Checkbox("##sk_visible", &sk.IsVisible);

            ImGui::Separator();
            LabeledField("CrossFade", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##sk_cf_target", m_crossFadeTarget, sizeof(m_crossFadeTarget));

            LabeledField("Blend Dur", 95.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##sk_cf_dur", &m_crossFadeDuration, 0.02f, 0.0f, 5.0f);

            if (ImGui::Button("CrossFade##sk_cf_btn", { ImGui::GetContentRegionAvail().x, 0.0f }))
            {
                if (m_crossFadeTarget[0] != '\0')
                {
                    sk.CrossFade(m_crossFadeTarget, m_crossFadeDuration);
                }
            }

            if (!sk.PreviousAnimation.empty())
            {
                ImGui::TextDisabled("Blending from '%s' (%.2f / %.2f)",
                    sk.PreviousAnimation.c_str(), sk.BlendTime, sk.BlendDuration);
            }
            ImGui::TextDisabled("Bone Palette: %zu", sk.BonePalette.size());
            ImGui::Spacing();
        }

        void DrawScriptComponent(Entity ent)
        {
            if (!m_scene->HasComponent<ScriptComponent>(ent)) return;

            bool remove = false;
            if (!ComponentHeader("Script", &remove)) return;
            if (remove) { m_scene->RemoveComponent<ScriptComponent>(ent); return; }

            auto& sc = m_scene->GetComponent<ScriptComponent>(ent);

            ImGui::Spacing();
            LabeledField("Enabled", 90.0f);
            ImGui::Checkbox("##script_enabled", &sc.Enabled);

            LabeledField("Script Name", 90.0f);
            char scriptBuf[256]{};
            std::strncpy(scriptBuf, sc.ScriptName.c_str(), sizeof(scriptBuf) - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##script_name", scriptBuf, sizeof(scriptBuf)))
            {
                sc.ScriptName = scriptBuf;
            }

            LabeledField("Script Path", 90.0f);
            char pathBuf[256]{};
            std::strncpy(pathBuf, sc.ScriptPath.c_str(), sizeof(pathBuf) - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##script_path", pathBuf, sizeof(pathBuf)))
            {
                sc.ScriptPath = pathBuf;
            }

            if (ImGui::Button("Reload Script##script_reload", { ImGui::GetContentRegionAvail().x, 0.0f }))
            {
                if (auto* engine = ScriptEngine::GetActive())
                {
                    engine->ReloadEntityScript(ent, *m_scene);
                }
            }
            ImGui::Spacing();
        }

        void DrawWorldEnvironmentSection()
        {
            if (!m_scene) return;
            if (!ImGui::CollapsingHeader("World Environment (Post-Processing)", ImGuiTreeNodeFlags_DefaultOpen)) return;

            auto& env = m_scene->GetEnvironment();
            ImGui::Spacing();

            LabeledField("Post-Process", 110.0f);
            ImGui::Checkbox("##env_pp_enabled", &env.PostProcessEnabled);

            LabeledField("Exposure", 110.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##env_exposure", &env.Exposure, 0.1f, 5.0f);

            LabeledField("Bloom Thresh", 110.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##env_bloom_thresh", &env.BloomThreshold, 0.0f, 5.0f);

            LabeledField("Bloom Int", 110.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##env_bloom_int", &env.BloomIntensity, 0.0f, 3.0f);

            LabeledField("Vignette", 110.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##env_vignette", &env.VignetteIntensity, 0.0f, 1.0f);

            ImGui::Spacing();
        }

        Scene*                m_scene = nullptr;
        std::optional<Entity> m_selected;
        char                  m_crossFadeTarget[128]{};
        f32                   m_crossFadeDuration = 0.25f;
    };

}
