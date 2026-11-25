#include "Core/Types.hpp"
#include "Core/Logger.hpp"
#include "Core/Timer.hpp"
#include "Platform/CrashHandler.hpp"
#include "Platform/Window.hpp"
#include "Platform/Input.hpp"
#include "Graphics/DX12/DX12Device.hpp"
#include "Graphics/DX11/DX11Device.hpp"
#include "Graphics/DX12/Vertex.hpp"
#include "Graphics/DX12/ShaderCompiler.hpp"
#include "Graphics/DX12/Pipeline.hpp"
#include "Graphics/DX12/DescriptorManager.hpp"
#include "Graphics/DX12/TextureManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneSerializer.hpp"
#include "Scene/Camera.hpp"
#include "Scene/Systems/RenderSystem.hpp"
#include "Scene/Systems/AnimationSystem.hpp"
#include "Assets/AssetImporters.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/LuaEntityBridge.hpp"
#include "Scene/StressTest.hpp"
#include "Editor/EditorLayer.hpp"
#include "Audio/AudioEngine.hpp"
#include "Audio/AudioSystem.hpp"
#include "Platform/FileSystem/VFS.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/Events/CommonEvents.hpp"
#include "Editor/History/CommandHistory.hpp"

#include <lua.hpp>
#include <filesystem>
#include <format>
#include <string_view>
#include <cstdlib>

namespace RL
{
    [[nodiscard]] inline std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativePath)
    {
        const std::string relStr = relativePath.string();
        if (relStr.find("://") != std::string::npos)
        {
            const auto resolved = VFS::Resolve(relStr);
            if (resolved) return *resolved;
        }

        const auto vfsResolved = VFS::Resolve("assets://" + relStr);
        if (vfsResolved) return *vfsResolved;

        if (std::filesystem::exists(relativePath))
        {
            return relativePath;
        }

        wchar_t exePathBuf[MAX_PATH];
        GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
        const std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();
        if (std::filesystem::exists(exeDir / relativePath))
        {
            return exeDir / relativePath;
        }

        const std::filesystem::path candidate = (exeDir / ".." / ".." / ".." / relativePath).lexically_normal();
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }

        return relativePath;
    }
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
    CrashHandler::Initialize();

    using namespace RL;

    Logger::SetMinLogLevel(LogLevel::Trace);
    RL_LOG_INFO("=================================================");
    RL_LOG_INFO("     RLengine 7.2 - Next-Generation Engine      ");
    RL_LOG_INFO("  1,000,000 Entities GPU Instanced Benchmark    ");
    RL_LOG_INFO("=================================================");

    bool smokeTest = false;
    u32 maxTestFrames = 15;
    bool stressTestMode = false;
    u32 stressEntityCount = 5000;
    bool useDX11 = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--dx11")
        {
            useDX11 = true;
        }
        else if (std::string_view(argv[i]) == "--smoke-test")
        {
            smokeTest = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
            {
                maxTestFrames = static_cast<u32>(std::atoi(argv[++i]));
                if (maxTestFrames == 0) maxTestFrames = 15;
            }
            RL_LOG_INFO("Running in automated smoke test mode (will render {} frames).", maxTestFrames);
        }
        else if (std::string_view(argv[i]) == "--stress-test")
        {
            stressTestMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
            {
                stressEntityCount = static_cast<u32>(std::atoi(argv[++i]));
                if (stressEntityCount == 0) stressEntityCount = 5000;
            }
            RL_LOG_INFO("Running in stress test mode (will spawn {} entities).", stressEntityCount);
        }
    }

    wchar_t exePathBuf[MAX_PATH];
    GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();

    auto findRoot = [&](const std::string& sub) -> std::filesystem::path {
        const std::filesystem::path sourceCandidate = (exeDir / ".." / ".." / ".." / sub).lexically_normal();
        if (std::filesystem::exists(sourceCandidate)) return sourceCandidate;
        if (std::filesystem::exists(sub)) return std::filesystem::absolute(sub);
        if (std::filesystem::exists(exeDir / sub)) return exeDir / sub;
        return std::filesystem::current_path() / sub;
    };

    const auto assetsRoot = findRoot("assets");
    const auto shadersRoot = findRoot("shaders");
    const auto scriptsRoot = findRoot("scripts");

    VFS::Mount("assets://", assetsRoot);
    VFS::Mount("shaders://", shadersRoot);
    VFS::Mount("shaders/", shadersRoot);
    VFS::Mount("scripts://", scriptsRoot);
    VFS::PrintMounts();
    AssetManager::RegisterDefaultImporters();

    if (smokeTest)
    {
        static_assert(sizeof(MeshVertex) == 96, "MeshVertex must be 96 bytes");
        static_assert(sizeof(InstanceData) == 176, "InstanceData must be 176 bytes");

        if (sizeof(MeshVertex) != 96)
        {
            RL_LOG_FATAL("Validation failed: sizeof(MeshVertex) is {}, expected 96", sizeof(MeshVertex));
            return -70;
        }

        if (sizeof(InstanceData) != 176)
        {
            RL_LOG_FATAL("Validation failed: sizeof(InstanceData) is {}, expected 176", sizeof(InstanceData));
            return -71;
        }

        MeshVertex defaultVert;
        if (defaultVert.Tangent.x != 1.0f || defaultVert.Tangent.y != 0.0f ||
            defaultVert.Tangent.z != 0.0f || defaultVert.Tangent.w != 1.0f)
        {
            RL_LOG_FATAL("Validation failed: MeshVertex default Tangent vector is incorrect");
            return -75;
        }

        InstanceData defaultInst;
        if (defaultInst.NormalMapIndex != 0 || defaultInst.MetallicRoughnessIndex != 0 || defaultInst.EmissiveIndex != 0)
        {
            RL_LOG_FATAL("Validation failed: InstanceData default PBR indices are incorrect");
            return -76;
        }

        MaterialComponent testMat;
        if (testMat.NormalMapIndex != 0 || !testMat.NormalMapPath.empty() ||
            testMat.MetallicRoughnessMapIndex != 0 || !testMat.MetallicRoughnessMapPath.empty() ||
            testMat.EmissiveMapIndex != 0 || !testMat.EmissiveMapPath.empty())
        {
            RL_LOG_FATAL("Validation failed: MaterialComponent default values for PBR slots are incorrect");
            return -72;
        }

        testMat.NormalMapIndex = 2;
        testMat.NormalMapPath = "textures/test_normal.png";
        testMat.MetallicRoughnessMapIndex = 3;
        testMat.MetallicRoughnessMapPath = "textures/test_mr.png";
        testMat.EmissiveMapIndex = 4;
        testMat.EmissiveMapPath = "textures/test_emissive.png";

        if (testMat.NormalMapIndex != 2 || testMat.NormalMapPath != "textures/test_normal.png" ||
            testMat.MetallicRoughnessMapIndex != 3 || testMat.MetallicRoughnessMapPath != "textures/test_mr.png" ||
            testMat.EmissiveMapIndex != 4 || testMat.EmissiveMapPath != "textures/test_emissive.png")
        {
            RL_LOG_FATAL("Validation failed: MaterialComponent PBR slot assignment failed");
            return -73;
        }

        RL_LOG_INFO("Smoke test: PBR slots and vertex/instance sizes validated successfully.");

        {
            Scene jointTestScene;
            PhysicsSystem jointPhys;

            Entity entDistA = jointTestScene.CreateEntity("DistA");
            auto& trDistA = jointTestScene.GetComponent<TransformComponent>(entDistA);
            trDistA.Position = { 0.0f, 0.0f, 0.0f };
            auto& rbDistA = jointTestScene.AddComponent<RigidBodyComponent>(entDistA);
            rbDistA.Type = BodyType::Dynamic;
            rbDistA.Mass = 1.0f;

            Entity entDistB = jointTestScene.CreateEntity("DistB");
            auto& trDistB = jointTestScene.GetComponent<TransformComponent>(entDistB);
            trDistB.Position = { 10.0f, 0.0f, 0.0f };
            auto& rbDistB = jointTestScene.AddComponent<RigidBodyComponent>(entDistB);
            rbDistB.Type = BodyType::Dynamic;
            rbDistB.Mass = 1.0f;

            auto& distJoint = jointTestScene.AddComponent<DistanceJointComponent>(entDistA);
            distJoint.TargetEntity = entDistB;
            distJoint.TargetDistance = 4.0f;
            distJoint.AutoDistance = false;
            distJoint.Stiffness = 1.0f;

            jointPhys.SolveConstraints(jointTestScene, 1.0f / 60.0f);

            const f32 curDistA = jointTestScene.GetComponent<TransformComponent>(entDistA).Position.x;
            const f32 curDistB = jointTestScene.GetComponent<TransformComponent>(entDistB).Position.x;
            const f32 newDist = std::abs(curDistB - curDistA);
            if (std::abs(newDist - 4.0f) > 0.01f)
            {
                RL_LOG_FATAL("DistanceJoint validation failed: distance {} != 4.0 (trA.x={}, trB.x={})", newDist, curDistA, curDistB);
                return -74;
            }

            Entity entAutoA = jointTestScene.CreateEntity("AutoA");
            auto& trAutoA = jointTestScene.GetComponent<TransformComponent>(entAutoA);
            trAutoA.Position = { 0.0f, 0.0f, 0.0f };

            Entity entAutoB = jointTestScene.CreateEntity("AutoB");
            auto& trAutoB = jointTestScene.GetComponent<TransformComponent>(entAutoB);
            trAutoB.Position = { 7.5f, 0.0f, 0.0f };

            auto& autoJoint = jointTestScene.AddComponent<DistanceJointComponent>(entAutoA);
            autoJoint.TargetEntity = entAutoB;
            autoJoint.AutoDistance = true;

            jointPhys.SolveConstraints(jointTestScene, 1.0f / 60.0f);

            if (autoJoint.AutoDistance || std::abs(autoJoint.TargetDistance - 7.5f) > 0.01f)
            {
                RL_LOG_FATAL("DistanceJoint AutoDistance validation failed: dist {}", autoJoint.TargetDistance);
                return -75;
            }

            Entity entStaticAnchor = jointTestScene.CreateEntity("StaticAnchor");
            jointTestScene.GetComponent<TransformComponent>(entStaticAnchor).Position = { 0.0f, 0.0f, 0.0f };
            auto& rbStatic = jointTestScene.AddComponent<RigidBodyComponent>(entStaticAnchor);
            rbStatic.Type = BodyType::Static;

            Entity entDynChild = jointTestScene.CreateEntity("DynChild");
            jointTestScene.GetComponent<TransformComponent>(entDynChild).Position = { 8.0f, 0.0f, 0.0f };
            auto& rbDynChild = jointTestScene.AddComponent<RigidBodyComponent>(entDynChild);
            rbDynChild.Type = BodyType::Dynamic;
            rbDynChild.Mass = 2.0f;

            auto& distJointStatic = jointTestScene.AddComponent<DistanceJointComponent>(entDynChild);
            distJointStatic.TargetEntity = entStaticAnchor;
            distJointStatic.TargetDistance = 3.0f;
            distJointStatic.AutoDistance = false;
            distJointStatic.Stiffness = 1.0f;

            jointPhys.SolveConstraints(jointTestScene, 1.0f / 60.0f);

            const f32 staticPosX = jointTestScene.GetComponent<TransformComponent>(entStaticAnchor).Position.x;
            const f32 dynChildPosX = jointTestScene.GetComponent<TransformComponent>(entDynChild).Position.x;
            if (std::abs(staticPosX) > 0.001f || std::abs(dynChildPosX - 3.0f) > 0.01f)
            {
                RL_LOG_FATAL("DistanceJoint static anchor validation failed: static {}, dyn {}", staticPosX, dynChildPosX);
                return -76;
            }

            Entity entHinge = jointTestScene.CreateEntity("HingeBody");
            jointTestScene.GetComponent<TransformComponent>(entHinge).Position = { 0.0f, 0.0f, 0.0f };
            auto& rbHinge = jointTestScene.AddComponent<RigidBodyComponent>(entHinge);
            rbHinge.Type = BodyType::Dynamic;
            rbHinge.AngularVelocity = { 3.0f, 4.0f, 5.0f };

            auto& hingeJoint = jointTestScene.AddComponent<HingeJointComponent>(entHinge);
            hingeJoint.TargetEntity = entStaticAnchor;
            hingeJoint.Anchor = { 0.0f, 0.0f, 0.0f };
            hingeJoint.Axis = { 0.0f, 1.0f, 0.0f };
            hingeJoint.EnableLimits = false;

            jointPhys.SolveConstraints(jointTestScene, 1.0f / 60.0f);

            const auto& curRbHinge = jointTestScene.GetComponent<RigidBodyComponent>(entHinge);
            if (std::abs(curRbHinge.AngularVelocity.x) > 0.01f || std::abs(curRbHinge.AngularVelocity.z) > 0.01f || std::abs(curRbHinge.AngularVelocity.y - 4.0f) > 0.01f)
            {
                RL_LOG_FATAL("HingeJoint angular axis projection failed: angVel ({}, {}, {})", curRbHinge.AngularVelocity.x, curRbHinge.AngularVelocity.y, curRbHinge.AngularVelocity.z);
                return -77;
            }

            hingeJoint.EnableLimits = true;
            hingeJoint.LowerAngleLimit = -0.5f;
            hingeJoint.UpperAngleLimit = 0.5f;
            jointTestScene.GetComponent<TransformComponent>(entHinge).Rotation = { 0.0f, 1.5f, 0.0f };
            jointTestScene.GetComponent<RigidBodyComponent>(entHinge).AngularVelocity = { 0.0f, 2.0f, 0.0f };

            jointPhys.SolveConstraints(jointTestScene, 1.0f / 60.0f);

            const auto& curTrHinge = jointTestScene.GetComponent<TransformComponent>(entHinge);
            const auto& curRbHingeAfter = jointTestScene.GetComponent<RigidBodyComponent>(entHinge);
            if (curTrHinge.Rotation.y > 0.51f || curRbHingeAfter.AngularVelocity.y > 0.01f)
            {
                RL_LOG_FATAL("HingeJoint limit enforcement failed: rotY {}, angVelY {}", curTrHinge.Rotation.y, curRbHingeAfter.AngularVelocity.y);
                return -78;
            }

            Entity entFixed = jointTestScene.CreateEntity("FixedBody");
            jointTestScene.GetComponent<TransformComponent>(entFixed).Position = { 0.0f, 0.0f, 0.0f };
            auto& rbFixed = jointTestScene.AddComponent<RigidBodyComponent>(entFixed);
            rbFixed.Type = BodyType::Dynamic;
            rbFixed.Mass = 1.0f;

            auto& fixedJoint = jointTestScene.AddComponent<FixedJointComponent>(entFixed);
            fixedJoint.TargetEntity = entStaticAnchor;
            fixedJoint.RelativeOffset = { 2.0f, 3.0f, 4.0f };
            fixedJoint.RelativeRotation = { 0.1f, 0.2f, 0.3f };
            fixedJoint.BreakForce = 0.0f;

            jointPhys.SolveConstraints(jointTestScene, 1.0f / 60.0f);

            const auto& curTrFixed = jointTestScene.GetComponent<TransformComponent>(entFixed);
            if (std::abs(curTrFixed.Position.x - 2.0f) > 0.01f ||
                std::abs(curTrFixed.Position.y - 3.0f) > 0.01f ||
                std::abs(curTrFixed.Position.z - 4.0f) > 0.01f)
            {
                RL_LOG_FATAL("FixedJoint lock validation failed: pos ({}, {}, {})", curTrFixed.Position.x, curTrFixed.Position.y, curTrFixed.Position.z);
                return -79;
            }

            bool breakTriggerFired = false;
            auto breakSub = EventBus::Subscribe<PhysicsTriggerEvent>([&](const PhysicsTriggerEvent& ev) {
                if (ev.triggerEntity == entFixed && ev.state == TriggerEvent::State::Exit)
                {
                    breakTriggerFired = true;
                }
            });

            jointTestScene.GetComponent<TransformComponent>(entFixed).Position = { 1000.0f, 0.0f, 0.0f };
            fixedJoint.BreakForce = 5.0f;
            fixedJoint.IsBroken = false;

            jointPhys.SolveConstraints(jointTestScene, 1.0f / 60.0f);

            if (!fixedJoint.IsBroken || !breakTriggerFired)
            {
                RL_LOG_FATAL("FixedJoint break validation failed: isBroken {}, triggerFired {}", fixedJoint.IsBroken, breakTriggerFired);
                return -80;
            }

            RL_LOG_INFO("Smoke test: DistanceJoint, HingeJoint, and FixedJoint constraints validated successfully.");
        }

        {
            auto scriptAsset = AssetManager::Load<ScriptAsset>("scripts://main.lua");
            auto meshAsset = AssetManager::Load<MeshAsset>("assets://models/cube.gltf");
            auto texAsset = AssetManager::Load<TextureAsset>("assets://textures/gem.png");
            if (!scriptAsset.IsValid() || scriptAsset->source.empty() ||
                !meshAsset.IsValid() || meshAsset->vertices.empty() || meshAsset->indices.empty() ||
                !texAsset.IsValid() || texAsset->width == 0 || texAsset->height == 0)
            {
                RL_LOG_FATAL("Alpha 3 AssetManager concrete importer validation failed");
                return -90;
            }

            Scene animTestScene;
            AnimationSystem animSys;
            animTestScene.SetAnimationSystem(&animSys);
            animSys.CreateProceduralTestRig("Alpha3TestRig");

            Entity skelEnt = animTestScene.CreateEntity("SkelBot");
            auto& sk = animTestScene.AddComponent<SkinnedMeshComponent>(skelEnt);
            sk.ModelPath = "Alpha3TestRig";
            sk.Play("Wave");
            animTestScene.Update(0.25f);

            const auto& postSk = animTestScene.GetComponent<SkinnedMeshComponent>(skelEnt);
            if (postSk.BonePalette.size() < 2)
            {
                RL_LOG_FATAL("Alpha 3 AnimationSystem BonePalette validation failed: size {}", postSk.BonePalette.size());
                return -91;
            }
            const DirectX::XMFLOAT4X4& b1 = postSk.BonePalette[1];
            const f32 diffFromId = std::abs(b1._11 - 1.0f) + std::abs(b1._12) + std::abs(b1._13) +
                                   std::abs(b1._21) + std::abs(b1._22 - 1.0f) + std::abs(b1._41) + std::abs(b1._42);
            if (diffFromId < 1e-4f)
            {
                RL_LOG_FATAL("Alpha 3 AnimationSystem FK bone interpolation did not update child bone matrix");
                return -92;
            }

            Scene serScene;
            serScene.GetEnvironment().SkyColor = { 0.12f, 0.22f, 0.32f, 1.0f };
            serScene.GetEnvironment().AmbientStrength = 1.65f;
            serScene.GetEnvironment().FogEnabled = true;
            serScene.GetEnvironment().FogDensity = 0.075f;
            serScene.GetEnvironment().FogColor = { 0.4f, 0.5f, 0.6f };

            Entity jA = serScene.CreateEntity("JointNodeA");
            Entity jB = serScene.CreateEntity("JointNodeB");
            auto& dj = serScene.AddComponent<DistanceJointComponent>(jA);
            dj.TargetEntity = jB;
            dj.TargetDistance = 3.5f;
            dj.AutoDistance = false;
            auto& hj = serScene.AddComponent<HingeJointComponent>(jA);
            hj.TargetEntity = jB;
            hj.EnableLimits = true;
            hj.EnableMotor = true;
            hj.MotorSpeed = 2.0f;
            auto& fj = serScene.AddComponent<FixedJointComponent>(jA);
            fj.TargetEntity = jB;
            fj.BreakForce = 42.0f;

            const std::string jPath = (std::filesystem::temp_directory_path() / "rlengine_a3_joints.json").string();
            const std::string bPath = (std::filesystem::temp_directory_path() / "rlengine_a3_joints.bin").string();
            if (!SceneSerializer::Save(serScene, jPath) || !SceneSerializer::Save(serScene, bPath))
            {
                RL_LOG_FATAL("Alpha 3 Joint/Environment Save failed");
                return -93;
            }

            Scene loadedJ, loadedB;
            const bool loadJOk = SceneSerializer::Load(loadedJ, nullptr, jPath);
            const bool loadBOk = SceneSerializer::Load(loadedB, nullptr, bPath);
            std::error_code a3Ec;
            std::filesystem::remove(jPath, a3Ec);
            std::filesystem::remove(bPath, a3Ec);
            if (!loadJOk || !loadBOk || loadedJ.GetAliveEntityCount() != 2 || loadedB.GetAliveEntityCount() != 2)
            {
                RL_LOG_FATAL("Alpha 3 Joint/Environment Load failed");
                return -94;
            }

            auto verifyLoaded = [](const Scene& s) -> bool {
                const auto& env = s.GetEnvironment();
                if (!env.FogEnabled || std::abs(env.FogDensity - 0.075f) > 1e-4f ||
                    std::abs(env.AmbientStrength - 1.65f) > 1e-4f || std::abs(env.SkyColor.x - 0.12f) > 1e-4f)
                {
                    return false;
                }
                const auto ents = s.GetAliveEntities();
                const auto* ldj = s.TryGetComponent<DistanceJointComponent>(ents[0]);
                const auto* lhj = s.TryGetComponent<HingeJointComponent>(ents[0]);
                const auto* lfj = s.TryGetComponent<FixedJointComponent>(ents[0]);
                if (!ldj || !lhj || !lfj) return false;
                if (ldj->TargetEntity != ents[1] || lhj->TargetEntity != ents[1] || lfj->TargetEntity != ents[1]) return false;
                if (std::abs(ldj->TargetDistance - 3.5f) > 1e-4f || !lhj->EnableMotor || std::abs(lfj->BreakForce - 42.0f) > 1e-4f) return false;
                return true;
            };

            if (!verifyLoaded(loadedJ) || !verifyLoaded(loadedB))
            {
                RL_LOG_FATAL("Alpha 3 Joint/Environment round-trip value mismatch");
                return -95;
            }

            RL_LOG_INFO("Smoke test Alpha 3: AssetManager, AnimationSystem, Joints & WorldEnvironment serialization validated successfully.");

            AssetManager::Clear();
            bool asyncMeshLoaded = false;
            AssetManager::LoadAsync<MeshAsset>("assets://models/cube.gltf", [&](AssetHandle<MeshAsset> h) {
                asyncMeshLoaded = h.IsValid() && h.Get() && !h.Get()->vertices.empty();
            });
            AssetManager::WaitAllAsync();
            if (!asyncMeshLoaded)
            {
                RL_LOG_FATAL("Alpha 4 AssetManager::LoadAsync<MeshAsset> validation failed");
                return -100;
            }

            sk.Play("Idle");
            animTestScene.Update(0.1f);
            const f32 idleVal = animTestScene.GetComponent<SkinnedMeshComponent>(skelEnt).BonePalette[1]._11;
            sk.CrossFade("Wave", 0.5f);
            animTestScene.Update(0.25f);
            const f32 blendVal = animTestScene.GetComponent<SkinnedMeshComponent>(skelEnt).BonePalette[1]._11;
            if (std::abs(blendVal - idleVal) < 1e-4f)
            {
                RL_LOG_FATAL("Alpha 4 AnimationSystem CrossFade blending validation failed");
                return -101;
            }

            Scene serV3;
            serV3.GetEnvironment().PostProcessEnabled = true;
            serV3.GetEnvironment().Exposure = 1.45f;
            serV3.GetEnvironment().BloomThreshold = 0.85f;
            serV3.GetEnvironment().BloomIntensity = 0.65f;
            serV3.GetEnvironment().VignetteIntensity = 0.45f;
            Entity v3Ent = serV3.CreateEntity("V3Actor");
            auto& pe = serV3.AddComponent<ParticleEmitterComponent>(v3Ent);
            pe.MaxParticles = 512;
            pe.EmissionRate = 75.0f;
            auto& as = serV3.AddComponent<AudioSourceComponent>(v3Ent);
            as.SoundPath = "assets://audio/test.wav";
            as.Volume = 0.75f;
            serV3.AddComponent<AudioListenerComponent>(v3Ent).IsActive = true;
            auto& v3Sk = serV3.AddComponent<SkinnedMeshComponent>(v3Ent);
            v3Sk.ModelPath = "Alpha3TestRig";
            v3Sk.CurrentAnimation = "Wave";
            v3Sk.Loop = false;

            const std::string v3J = (std::filesystem::temp_directory_path() / "rlengine_a4_v3.json").string();
            const std::string v3B = (std::filesystem::temp_directory_path() / "rlengine_a4_v3.bin").string();
            SceneSerializer::Save(serV3, v3J);
            SceneSerializer::Save(serV3, v3B);
            Scene loadV3J, loadV3B;
            const bool okV3J = SceneSerializer::Load(loadV3J, nullptr, v3J);
            const bool okV3B = SceneSerializer::Load(loadV3B, nullptr, v3B);
            std::error_code a4Ec;
            std::filesystem::remove(v3J, a4Ec);
            std::filesystem::remove(v3B, a4Ec);
            auto checkV3 = [](const Scene& s) -> bool {
                const auto& env = s.GetEnvironment();
                if (!env.PostProcessEnabled || std::abs(env.Exposure - 1.45f) > 1e-4f ||
                    std::abs(env.BloomThreshold - 0.85f) > 1e-4f || std::abs(env.BloomIntensity - 0.65f) > 1e-4f ||
                    std::abs(env.VignetteIntensity - 0.45f) > 1e-4f) return false;
                const auto ents = s.GetAliveEntities();
                if (ents.size() != 1) return false;
                const auto* p = s.TryGetComponent<ParticleEmitterComponent>(ents[0]);
                const auto* a = s.TryGetComponent<AudioSourceComponent>(ents[0]);
                const auto* l = s.TryGetComponent<AudioListenerComponent>(ents[0]);
                const auto* k = s.TryGetComponent<SkinnedMeshComponent>(ents[0]);
                return p && p->MaxParticles == 512 && a && a->SoundPath == "assets://audio/test.wav" &&
                       l && l->IsActive && k && k->CurrentAnimation == "Wave" && !k->Loop;
            };
            if (!okV3J || !okV3B || !checkV3(loadV3J) || !checkV3(loadV3B))
            {
                RL_LOG_FATAL("Alpha 4 Binary v3 / JSON serialization validation failed");
                return -102;
            }

            {
                const std::filesystem::path tmpLua = std::filesystem::temp_directory_path() / "rlengine_a4_entity.lua";
                {
                    std::ofstream ofs(tmpLua);
                    ofs << "function OnStart(self)\n  self:SetPosition(1.0, 2.0, 3.0)\nend\n"
                        << "function OnUpdate(self, dt)\n  local x, y, z = self:GetPosition()\n  self:SetPosition(x + 1.0, y, z)\nend\n";
                }
                Scene scScene;
                Entity scEnt = scScene.CreateEntity("ScriptedBot");
                auto& scComp = scScene.AddComponent<ScriptComponent>(scEnt);
                scComp.ScriptPath = tmpLua.string();
                scComp.Enabled = true;

                ScriptEngine seTest;
                if (const auto mainLua = VFS::Resolve("scripts://main.lua"); mainLua && seTest.Initialize(*mainLua, &scScene))
                {
                    seTest.StartEntityScripts(scScene);
                    seTest.UpdateEntityScripts(scScene, 0.016f);
                    seTest.StopEntityScripts();
                    seTest.Shutdown();
                }
                std::filesystem::remove(tmpLua, a4Ec);
                const auto& pos = scScene.GetComponent<TransformComponent>(scEnt).Position;
                if (std::abs(pos.x - 2.0f) > 1e-4f || std::abs(pos.y - 2.0f) > 1e-4f)
                {
                    RL_LOG_FATAL("Alpha 4 ScriptComponent OnStart/OnUpdate validation failed ({}, {})", pos.x, pos.y);
                    return -103;
                }
            }

            RL_LOG_INFO("Smoke test Alpha 4: Async AssetManager, Animation CrossFade, Binary v3 & Entity ScriptComponent validated successfully.");
        }
    }

    WindowDesc windowDesc{
        .title = L"RLengine 7.2 [DirectX 12 + Lua 5.4 | ECS Scene]",
        .width = 1600,
        .height = 900,
        .resizable = true
    };

    Window window;
    auto windowInitRes = window.Initialize(windowDesc);
    if (!windowInitRes)
    {
        RL_LOG_FATAL("Window initialization failed: {}", windowInitRes.error());
        return -1;
    }

    DX12Device device;
    if (!useDX11)
    {
        auto deviceInitRes = device.Initialize(window.GetHWND(), window.GetWidth(), window.GetHeight());
        if (!deviceInitRes)
        {
            RL_LOG_WARN("DirectX 12 initialization failed ({}); automatically falling back to DirectX 11...", deviceInitRes.error());
            useDX11 = true;
        }
    }

    if (useDX11)
    {
        RL_LOG_INFO("Operating in DirectX 11 Fallback / Legacy mode (GTX 500-700 / Mobile / Wine / Feature Level 11_0)...");
        DX11Device dx11;
        auto d11Res = dx11.Initialize(window.GetHWND(), window.GetWidth(), window.GetHeight());
        if (!d11Res)
        {
            RL_LOG_FATAL("DirectX 11 initialization failed: {}", d11Res.error());
            return -2;
        }

        Scene scene;
        scene.GetCamera().SetProjectionType(CameraProjection::Perspective);
        scene.GetCamera().SetPosition({ 0.0f, 2.0f, -5.0f });
        scene.GetCamera().SetRotation({ 0.2f, 0.0f, 0.0f });
        scene.GetCamera().SetAspectRatio(window.GetAspectRatio());

        window.SetResizeCallback([&dx11, &scene](u32 width, u32 height) {
            EventBus::Publish(WindowResizedEvent{ width, height });
            dx11.Resize(width, height);
            if (height > 0)
            {
                scene.GetCamera().SetAspectRatio(static_cast<f32>(width) / static_cast<f32>(height));
            }
        });

        ScriptEngine scriptEngine;
        const auto scriptPath = ResolveAssetPath("scripts/main.lua");
        auto scriptInitRes = scriptEngine.Initialize(scriptPath, &scene, nullptr, &window);
        if (!scriptInitRes)
        {
            RL_LOG_WARN("Script engine initialization: {}", scriptInitRes.error());
        }

        EditorLayer editorLayer;
        auto editorInitRes = editorLayer.InitializeDX11(
            window.GetHWND(),
            dx11.GetDevice(),
            dx11.GetContext(),
            &scriptEngine
        );
        if (!editorInitRes)
        {
            RL_LOG_FATAL("EditorLayer DX11 initialization failed: {}", editorInitRes.error());
            return -11;
        }

        scene.SetPhysicsSystem(&editorLayer.GetSceneBuilder().GetPhysicsSystem());

        AnimationSystem animationSystem;
        scene.SetAnimationSystem(&animationSystem);

        auto audioInitRes = AudioEngine::Get().Initialize();
        if (!audioInitRes)
        {
            RL_LOG_WARN("AudioEngine initialization: {}", audioInitRes.error());
        }
        AudioSystem audioSystem;
        scene.SetAudioSystem(&audioSystem);

        StressTest stressTest;
        LuaEntityBridge::SetStressTest(&stressTest);

        Entity cube = scene.CreateEntity("Cube");
        scene.AddComponent<MeshComponent>(cube, "Cube");
        scene.AddComponent<MaterialComponent>(cube);
        scene.GetComponent<TransformComponent>(cube).Position = { 0.0f, 0.0f, 0.0f };

        Entity child = scene.CreateEntity("ChildCube");
        scene.AddComponent<MeshComponent>(child, "Sphere");
        scene.GetComponent<TransformComponent>(child).Position = { 1.5f, 0.5f, 0.0f };
        scene.SetParent(child, cube);

        if (smokeTest)
        {
            Entity planeEnt = scene.CreateEntity("SmokePlane");
            scene.AddComponent<MeshComponent>(planeEnt, "Plane");
            scene.GetComponent<TransformComponent>(planeEnt).Position = { 0.0f, -1.0f, 0.0f };
        }

        Timer& timer = window.GetTimer();
        timer.Reset();

        f32 titleUpdateTimer = 0.0f;
        RL_LOG_INFO("All DirectX 11 subsystems initialized successfully. Entering engine main loop...");

        while (true)
        {
            Input::NewFrame();
            EventBus::DispatchDeferred();
            AssetManager::PollHotReload();
            AssetManager::PollAsyncLoads();
            AudioEngine::Get().Update();
            if (!window.ProcessMessages() || window.ShouldClose()) break;

            timer.Tick();
            const f32 dt = timer.GetDeltaTime();

            if (window.IsMinimized())
            {
                Sleep(10);
                continue;
            }

            if (editorLayer.GetSceneBuilder().IsSimulationRunning())
            {
                scriptEngine.Tick(dt);
                scene.Update(dt);
                stressTest.Update(scene, dt);
            }

            titleUpdateTimer += dt;
            if (titleUpdateTimer >= 0.25f)
            {
                titleUpdateTimer = 0.0f;
                const auto title = std::format(
                    "RLengine 7.2 | GPU: {} | {}x{} | {} FPS ({:.2f} ms) | ECS Entities: {} [DirectX 11]",
                    dx11.GetGpuDescription(),
                    window.GetWidth(),
                    window.GetHeight(),
                    timer.GetFPS(),
                    timer.GetFrameTimeMS(),
                    scene.GetAliveEntityCount()
                );
                window.SetTitle(title);
            }

            editorLayer.BeginFrame();
            editorLayer.RenderUI(timer, dx11, scene, static_cast<u32>(scene.GetAliveEntityCount()), nullptr, nullptr, &scriptEngine);

            dx11.BeginFrame();
            const auto& dx11Sky = scene.GetEnvironment().SkyColor;
            dx11.ClearRenderTarget(dx11Sky.x, dx11Sky.y, dx11Sky.z, dx11Sky.w);
            dx11.RenderScene(scene);
            editorLayer.EndFrameDX11();
            dx11.EndFrame();

            const bool vsync = editorLayer.GetSceneBuilder().IsVSyncEnabled();
            dx11.Present(vsync);

            if (smokeTest && timer.GetFrameCount() >= maxTestFrames)
            {
                if (!dx11.HasCachedMesh("Sphere") || dx11.GetCachedMeshIndexCount("Sphere") != 9216 ||
                    !dx11.HasCachedMesh("Plane") || dx11.GetCachedMeshIndexCount("Plane") != 600)
                {
                    RL_LOG_FATAL("DirectX 11 smoke test failed: multi-mesh cache missing Sphere/Plane buffers");
                    return -96;
                }
                const usize originalCount = scene.GetAliveEntityCount();
                const std::string jsonPath = (std::filesystem::temp_directory_path() / "rlengine_smoke_test.json").string();
                const std::string binPath = (std::filesystem::temp_directory_path() / "rlengine_smoke_test.bin").string();
                SceneSerializer::Save(scene, jsonPath);
                SceneSerializer::Save(scene, binPath);
                Scene jsonScene;
                SceneSerializer::Load(jsonScene, nullptr, jsonPath);
                Scene binScene;
                SceneSerializer::Load(binScene, nullptr, binPath);
                std::error_code ec;
                std::filesystem::remove(jsonPath, ec);
                std::filesystem::remove(binPath, ec);
                if (jsonScene.GetAliveEntityCount() != originalCount || binScene.GetAliveEntityCount() != originalCount)
                {
                    RL_LOG_FATAL("DirectX 11 smoke test entity count mismatch: original {}, json {}, bin {}",
                        originalCount, jsonScene.GetAliveEntityCount(), binScene.GetAliveEntityCount());
                    return -12;
                }
                RL_LOG_INFO("DirectX 11 Smoke test completed successfully ({} frames rendered, multi-mesh cache + entity count verified: {}).", timer.GetFrameCount(), originalCount);
                break;
            }
        }
        window.SetResizeCallback(nullptr);
        dx11.WaitForGPU();
        editorLayer.Shutdown();
        AssetManager::Shutdown();
        AudioEngine::Get().Shutdown();
        scriptEngine.Shutdown();
        dx11.Shutdown();
        window.Shutdown();
        RL_LOG_INFO("DirectX 11 shutdown completed cleanly.");
        return 0;
    }

    D3D12_VIEWPORT viewport{
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = static_cast<f32>(window.GetWidth()),
        .Height = static_cast<f32>(window.GetHeight()),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f
    };

    D3D12_RECT scissorRect{
        .left = 0,
        .top = 0,
        .right = static_cast<LONG>(window.GetWidth()),
        .bottom = static_cast<LONG>(window.GetHeight())
    };

    Scene scene;
    scene.GetCamera().SetAspectRatio(window.GetAspectRatio());

    window.SetResizeCallback([&device, &viewport, &scissorRect, &scene](u32 width, u32 height) {
        EventBus::Publish(WindowResizedEvent{ width, height });
        device.Resize(width, height);
        viewport.Width = static_cast<f32>(width);
        viewport.Height = static_cast<f32>(height);
        scissorRect.right = static_cast<LONG>(width);
        scissorRect.bottom = static_cast<LONG>(height);
        if (height > 0)
        {
            scene.GetCamera().SetAspectRatio(static_cast<f32>(width) / static_cast<f32>(height));
        }
    });

    ShaderCompiler shaderCompiler;
    auto scInitRes = shaderCompiler.Initialize();
    if (!scInitRes)
    {
        RL_LOG_FATAL("Shader compiler initialization failed: {}", scInitRes.error());
        return -3;
    }

    const auto shaderPath = ResolveAssetPath("shaders/quad.hlsl");
    RL_LOG_INFO("Compiling shaders from '{}'...", shaderPath.string());

    auto vsBlobRes = shaderCompiler.CompileVertexShader(shaderPath, "VSMain", "6_5");
    if (!vsBlobRes)
    {
        RL_LOG_FATAL("Vertex shader compilation failed: {}", vsBlobRes.error());
        return -4;
    }

    auto psBlobRes = shaderCompiler.CompilePixelShader(shaderPath, "PSMain", "6_5");
    if (!psBlobRes)
    {
        RL_LOG_FATAL("Pixel shader compilation failed: {}", psBlobRes.error());
        return -5;
    }

    const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(Vertex, Position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, Color),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(Vertex, UV),       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    PipelineConfig quadCfg{};
    quadCfg.TopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    quadCfg.CullMode = D3D12_CULL_MODE_NONE;
    quadCfg.FillMode = D3D12_FILL_MODE_SOLID;
    quadCfg.RtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    quadCfg.EnableBlend = true;
    quadCfg.EnableDepth = true;
    quadCfg.DepthWrite = false;
    quadCfg.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    quadCfg.DsvFormat = DXGI_FORMAT_D32_FLOAT;

    Pipeline pipeline;
    auto pipeInitRes = pipeline.Initialize(
        device.GetDevice(),
        vsBlobRes->Get(),
        psBlobRes->Get(),
        inputElements,
        quadCfg
    );
    if (!pipeInitRes)
    {
        RL_LOG_FATAL("Pipeline creation failed: {}", pipeInitRes.error());
        return -6;
    }

    DescriptorManager descriptorManager;
    auto dmInitRes = descriptorManager.Initialize(device.GetDevice(), 1024);
    if (!dmInitRes)
    {
        RL_LOG_FATAL("DescriptorManager initialization failed: {}", dmInitRes.error());
        return -7;
    }

    TextureManager textureManager;
    auto tmInitRes = textureManager.Initialize(device, descriptorManager);
    if (!tmInitRes)
    {
        RL_LOG_FATAL("TextureManager initialization failed: {}", tmInitRes.error());
        return -8;
    }

    RenderSystem renderSystem;
    auto rsInitRes = renderSystem.Initialize(device.GetDevice(), &descriptorManager, &textureManager, &shaderCompiler);
    if (!rsInitRes)
    {
        RL_LOG_FATAL("RenderSystem initialization failed: {}", rsInitRes.error());
        return -9;
    }
    renderSystem.SetQuadPipeline(&pipeline);
    scene.SetRenderSystem(&renderSystem);

    ScriptEngine scriptEngine;
    const auto scriptPath = ResolveAssetPath("scripts/main.lua");
    auto scriptInitRes = scriptEngine.Initialize(scriptPath, &scene, &textureManager, &window);
    if (!scriptInitRes)
    {
        RL_LOG_FATAL("Script engine initialization failed: {}", scriptInitRes.error());
        return -10;
    }

    EditorLayer editorLayer;
    auto editorInitRes = editorLayer.Initialize(
        window.GetHWND(),
        device.GetDevice(),
        NUM_FRAMES,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        &scriptEngine
    );
    if (!editorInitRes)
    {
        RL_LOG_FATAL("EditorLayer initialization failed: {}", editorInitRes.error());
        return -11;
    }

    auto partInitRes = editorLayer.GetSceneBuilder().GetParticleSystem().Initialize(
        device.GetDevice(),
        &descriptorManager,
        &shaderCompiler
    );
    if (!partInitRes)
    {
        RL_LOG_WARN("ParticleSystem initialization: {}", partInitRes.error());
    }
    scene.SetParticleSystem(&editorLayer.GetSceneBuilder().GetParticleSystem());
    scene.SetPhysicsSystem(&editorLayer.GetSceneBuilder().GetPhysicsSystem());

    AnimationSystem animationSystem;
    scene.SetAnimationSystem(&animationSystem);

    auto audioInitRes = AudioEngine::Get().Initialize();
    if (!audioInitRes)
    {
        RL_LOG_WARN("AudioEngine initialization: {}", audioInitRes.error());
    }
    AudioSystem audioSystem;
    scene.SetAudioSystem(&audioSystem);

    StressTest stressTest;
    LuaEntityBridge::SetStressTest(&stressTest);
    if (stressTestMode)
    {
        editorLayer.GetSceneBuilder().SetVSyncEnabled(false);
        scene.GetCamera().SetProjectionType(CameraProjection::Perspective);
        scene.GetCamera().SetPosition({ 0.0f, 0.0f, -30.0f });
        scene.GetCamera().SetRotation({ 0.0f, 0.0f, 0.0f });
        scene.GetCamera().SetFOV(DirectX::XMConvertToRadians(45.0f));
        scene.GetCamera().SetAspectRatio(window.GetAspectRatio());
        stressTest.Spawn(scene, stressEntityCount);
    }
    if (smokeTest)
    {
        Scene testScene;
        auto cmdCreate = std::make_unique<Editor::CreateEntityCommand>(&testScene, "TestBox", [&](Entity e) {
            testScene.AddComponent<MeshComponent>(e, "Cube");
            testScene.AddComponent<MaterialComponent>(e);
            testScene.AddComponent<LightComponent>(e);
            testScene.AddComponent<CameraComponent>(e);
            testScene.AddComponent<RigidBodyComponent>(e);
            testScene.AddComponent<BoxColliderComponent>(e);
            testScene.AddComponent<SphereColliderComponent>(e);
        });
        Editor::CommandHistory::Execute(std::move(cmdCreate));
        if (testScene.GetAliveEntityCount() != 1) return -20;
        Editor::CommandHistory::Undo();
        if (testScene.GetAliveEntityCount() != 0) return -21;
        Editor::CommandHistory::Redo();
        if (testScene.GetAliveEntityCount() != 1) return -22;

        const auto& testBoxMat = testScene.GetComponent<MaterialComponent>(testScene.GetAliveEntities()[0]);
        if (testBoxMat.NormalMapIndex != 0 || !testBoxMat.NormalMapPath.empty() ||
            testBoxMat.MetallicRoughnessMapIndex != 0 || !testBoxMat.MetallicRoughnessMapPath.empty() ||
            testBoxMat.EmissiveMapIndex != 0 || !testBoxMat.EmissiveMapPath.empty())
        {
            return -74;
        }

        auto& matToTest = testScene.GetComponent<MaterialComponent>(testScene.GetAliveEntities()[0]);
        matToTest.NormalMapIndex = 11;
        matToTest.NormalMapPath = "textures/box_normal.png";
        matToTest.MetallicRoughnessMapIndex = 12;
        matToTest.MetallicRoughnessMapPath = "textures/box_mr.png";
        matToTest.EmissiveMapIndex = 13;
        matToTest.EmissiveMapPath = "textures/box_emissive.png";

        const std::string matJsonPath = (std::filesystem::temp_directory_path() / "rlengine_mat_test.json").string();
        const std::string matBinPath = (std::filesystem::temp_directory_path() / "rlengine_mat_test.bin").string();
        SceneSerializer::Save(testScene, matJsonPath);
        SceneSerializer::Save(testScene, matBinPath);
        Scene roundtripJsonScene;
        SceneSerializer::Load(roundtripJsonScene, nullptr, matJsonPath);
        Scene roundtripBinScene;
        SceneSerializer::Load(roundtripBinScene, nullptr, matBinPath);
        std::error_code matEc;
        std::filesystem::remove(matJsonPath, matEc);
        std::filesystem::remove(matBinPath, matEc);

        const auto& jsonMat = roundtripJsonScene.GetComponent<MaterialComponent>(roundtripJsonScene.GetAliveEntities()[0]);
        const auto& binMat = roundtripBinScene.GetComponent<MaterialComponent>(roundtripBinScene.GetAliveEntities()[0]);

        if (jsonMat.NormalMapIndex != 11 || jsonMat.NormalMapPath != "textures/box_normal.png" ||
            jsonMat.MetallicRoughnessMapIndex != 12 || jsonMat.MetallicRoughnessMapPath != "textures/box_mr.png" ||
            jsonMat.EmissiveMapIndex != 13 || jsonMat.EmissiveMapPath != "textures/box_emissive.png")
        {
            RL_LOG_FATAL("Validation failed: MaterialComponent PBR slots JSON serialization roundtrip mismatch");
            return -77;
        }

        if (binMat.NormalMapIndex != 11 || binMat.NormalMapPath != "textures/box_normal.png" ||
            binMat.MetallicRoughnessMapIndex != 12 || binMat.MetallicRoughnessMapPath != "textures/box_mr.png" ||
            binMat.EmissiveMapIndex != 13 || binMat.EmissiveMapPath != "textures/box_emissive.png")
        {
            RL_LOG_FATAL("Validation failed: MaterialComponent PBR slots Binary serialization roundtrip mismatch");
            return -78;
        }

        Entity childEnt = testScene.CreateEntity("Child");
        Entity parentEnt = testScene.GetAliveEntities()[0];
        Editor::CommandHistory::Execute(std::make_unique<Editor::ReparentEntityCommand>(&testScene, childEnt, parentEnt));
        if (testScene.GetParent(childEnt) != parentEnt) return -23;
        Editor::CommandHistory::Undo();
        if (testScene.GetParent(childEnt).IsValid()) return -24;
        Editor::CommandHistory::Redo();
        if (testScene.GetParent(childEnt) != parentEnt) return -25;

        Editor::CommandHistory::Execute(std::make_unique<Editor::DuplicateEntityCommand>(&testScene, childEnt));
        if (testScene.GetAliveEntityCount() != 3) return -26;
        Editor::CommandHistory::Undo();
        if (testScene.GetAliveEntityCount() != 2) return -27;
        Editor::CommandHistory::Redo();
        if (testScene.GetAliveEntityCount() != 3) return -28;

        Editor::CommandHistory::Execute(std::make_unique<Editor::DeleteEntityCommand>(&testScene, parentEnt));
        if (testScene.GetAliveEntityCount() != 0) return -29;
        Editor::CommandHistory::Undo();
        if (testScene.GetAliveEntityCount() != 3) return -30;
        Editor::CommandHistory::Redo();
        if (testScene.GetAliveEntityCount() != 0) return -31;
        Editor::CommandHistory::Undo();
        if (testScene.GetAliveEntityCount() != 3) return -32;
        Editor::CommandHistory::Clear();

        {
            Scene trigScene;
            PhysicsSystem psTrig;
            trigScene.SetPhysicsSystem(&psTrig);

            Entity triggerEnt = trigScene.CreateEntity("TriggerZone");
            auto& trTrig = trigScene.AddComponent<TransformComponent>(triggerEnt);
            trTrig.Position = { 0.0f, 2.0f, 0.0f };
            auto& boxTrig = trigScene.AddComponent<BoxColliderComponent>(triggerEnt);
            boxTrig.HalfSize = { 1.0f, 1.0f, 1.0f };
            boxTrig.IsTrigger = true;

            Entity visitorEnt = trigScene.CreateEntity("Visitor");
            auto& trVis = trigScene.AddComponent<TransformComponent>(visitorEnt);
            trVis.Position = { 0.0f, 2.0f, 0.0f };
            auto& rbVis = trigScene.AddComponent<RigidBodyComponent>(visitorEnt);
            rbVis.Type = BodyType::Dynamic;
            rbVis.UseGravity = false;
            rbVis.LinearVelocity = { 0.0f, 0.0f, 0.0f };
            auto& boxVis = trigScene.AddComponent<BoxColliderComponent>(visitorEnt);
            boxVis.HalfSize = { 0.5f, 0.5f, 0.5f };

            psTrig.Step(trigScene, 1.0f / 60.0f);

            if (!psTrig.IsTriggerActive(triggerEnt)) return -50;
            if (!psTrig.HasTriggerOverlap(triggerEnt, visitorEnt)) return -51;
            const auto* resVisTr = trigScene.TryGetComponent<TransformComponent>(visitorEnt);
            if (!resVisTr || resVisTr->Position.x != 0.0f || resVisTr->Position.y != 2.0f || resVisTr->Position.z != 0.0f) return -52;
        }

        {
            Scene ccdScene;
            PhysicsSystem psCCD;
            ccdScene.SetPhysicsSystem(&psCCD);

            Entity ccdEnt = ccdScene.CreateEntity("HighSpeedProjectile");
            auto& trCCD = ccdScene.AddComponent<TransformComponent>(ccdEnt);
            trCCD.Position = { 0.0f, 1.0f, 0.0f };
            auto& rbCCD = ccdScene.AddComponent<RigidBodyComponent>(ccdEnt);
            rbCCD.Type = BodyType::Dynamic;
            rbCCD.UseGravity = false;
            rbCCD.LinearVelocity = { 0.0f, -600.0f, 0.0f };
            auto& boxCCD = ccdScene.AddComponent<BoxColliderComponent>(ccdEnt);
            boxCCD.HalfSize = { 0.5f, 0.5f, 0.5f };

            psCCD.Step(ccdScene, 1.0f / 60.0f);

            const auto* resTrCCD = ccdScene.TryGetComponent<TransformComponent>(ccdEnt);
            const auto* resRbCCD = ccdScene.TryGetComponent<RigidBodyComponent>(ccdEnt);
            if (!resTrCCD || !resRbCCD) return -53;
            if (resTrCCD->Position.y < 0.49f) return -53;
            if (resRbCCD->LinearVelocity.y <= 0.0f) return -54;
        }

        {
            Scene wallScene;
            PhysicsSystem psWall;
            wallScene.SetPhysicsSystem(&psWall);

            Entity wallEnt = wallScene.CreateEntity("StaticWall");
            auto& trWall = wallScene.AddComponent<TransformComponent>(wallEnt);
            trWall.Position = { 10.0f, 5.0f, 0.0f };
            auto& boxWall = wallScene.AddComponent<BoxColliderComponent>(wallEnt);
            boxWall.HalfSize = { 0.2f, 2.0f, 2.0f };

            Entity bulletEnt = wallScene.CreateEntity("HorizontalBullet");
            auto& trBullet = wallScene.AddComponent<TransformComponent>(bulletEnt);
            trBullet.Position = { 0.0f, 5.0f, 0.0f };
            auto& rbBullet = wallScene.AddComponent<RigidBodyComponent>(bulletEnt);
            rbBullet.Type = BodyType::Dynamic;
            rbBullet.UseGravity = false;
            rbBullet.LinearVelocity = { 900.0f, 0.0f, 0.0f };
            auto& boxBullet = wallScene.AddComponent<BoxColliderComponent>(bulletEnt);
            boxBullet.HalfSize = { 0.2f, 0.2f, 0.2f };

            psWall.Step(wallScene, 1.0f / 60.0f);

            const auto* resTrBullet = wallScene.TryGetComponent<TransformComponent>(bulletEnt);
            const auto* resRbBullet = wallScene.TryGetComponent<RigidBodyComponent>(bulletEnt);
            if (!resTrBullet || !resRbBullet) return -55;
            if (resTrBullet->Position.x > 10.1f) return -55;
            if (resRbBullet->LinearVelocity.x >= 0.0f) return -56;
        }

        {
            Scene slideScene;
            PhysicsSystem psSlide;
            slideScene.SetPhysicsSystem(&psSlide);

            Entity slideEnt = slideScene.CreateEntity("SlidingBox");
            auto& trSlide = slideScene.AddComponent<TransformComponent>(slideEnt);
            trSlide.Position = { 0.0f, 0.5f, 0.0f };
            auto& rbSlide = slideScene.AddComponent<RigidBodyComponent>(slideEnt);
            rbSlide.Type = BodyType::Dynamic;
            rbSlide.UseGravity = true;
            rbSlide.LinearVelocity = { 10.0f, 0.0f, 0.0f };
            auto& boxSlide = slideScene.AddComponent<BoxColliderComponent>(slideEnt);
            boxSlide.HalfSize = { 0.5f, 0.5f, 0.5f };

            psSlide.Step(slideScene, 1.0f / 60.0f);

            const auto* resTrSlide = slideScene.TryGetComponent<TransformComponent>(slideEnt);
            if (!resTrSlide || resTrSlide->Position.x <= 0.05f) return -57;
        }

        {
            Scene headOnScene;
            PhysicsSystem psHeadOn;
            headOnScene.SetPhysicsSystem(&psHeadOn);

            Entity entA = headOnScene.CreateEntity("BodyA");
            auto& trA = headOnScene.AddComponent<TransformComponent>(entA);
            trA.Position = { -5.0f, 5.0f, 0.0f };
            auto& rbA = headOnScene.AddComponent<RigidBodyComponent>(entA);
            rbA.Type = BodyType::Dynamic;
            rbA.UseGravity = false;
            rbA.LinearVelocity = { 600.0f, 0.0f, 0.0f };
            auto& sphereA = headOnScene.AddComponent<SphereColliderComponent>(entA);
            sphereA.Radius = 0.5f;

            Entity entB = headOnScene.CreateEntity("BodyB");
            auto& trB = headOnScene.AddComponent<TransformComponent>(entB);
            trB.Position = { 5.0f, 5.0f, 0.0f };
            auto& rbB = headOnScene.AddComponent<RigidBodyComponent>(entB);
            rbB.Type = BodyType::Dynamic;
            rbB.UseGravity = false;
            rbB.LinearVelocity = { -600.0f, 0.0f, 0.0f };
            auto& sphereB = headOnScene.AddComponent<SphereColliderComponent>(entB);
            sphereB.Radius = 0.5f;

            psHeadOn.Step(headOnScene, 1.0f / 60.0f);

            const auto* resTrA = headOnScene.TryGetComponent<TransformComponent>(entA);
            const auto* resTrB = headOnScene.TryGetComponent<TransformComponent>(entB);
            const auto* resRbA = headOnScene.TryGetComponent<RigidBodyComponent>(entA);
            const auto* resRbB = headOnScene.TryGetComponent<RigidBodyComponent>(entB);

            if (!resTrA || !resTrB || !resRbA || !resRbB) return -58;
            if (resTrA->Position.x >= resTrB->Position.x) return -58;
            if (resRbA->LinearVelocity.x >= 0.0f) return -59;
            if (resRbB->LinearVelocity.x <= 0.0f) return -60;
        }

        {
            Scene lifeScene;
            PhysicsSystem psLife;
            lifeScene.SetPhysicsSystem(&psLife);

            Entity trig = lifeScene.CreateEntity("LifecycleTrigger");
            auto& trT = lifeScene.AddComponent<TransformComponent>(trig);
            trT.Position = { 0.0f, 0.0f, 0.0f };
            auto& boxT = lifeScene.AddComponent<BoxColliderComponent>(trig);
            boxT.HalfSize = { 1.0f, 1.0f, 1.0f };
            boxT.IsTrigger = true;

            Entity agent = lifeScene.CreateEntity("Agent");
            auto& trAg = lifeScene.AddComponent<TransformComponent>(agent);
            trAg.Position = { 0.0f, 0.0f, 0.0f };
            auto& rbAg = lifeScene.AddComponent<RigidBodyComponent>(agent);
            rbAg.Type = BodyType::Dynamic;
            rbAg.UseGravity = false;
            auto& boxAg = lifeScene.AddComponent<BoxColliderComponent>(agent);
            boxAg.HalfSize = { 0.5f, 0.5f, 0.5f };

            psLife.Step(lifeScene, 1.0f / 60.0f);
            if (!psLife.HasTriggerOverlap(agent, trig)) return -61;
            if (psLife.GetTriggerEvents().empty()) return -62;
            if (psLife.GetTriggerEvents()[0].EventState != TriggerEvent::State::Enter) return -63;

            psLife.Step(lifeScene, 1.0f / 60.0f);
            if (psLife.GetTriggerEvents().empty()) return -64;
            if (psLife.GetTriggerEvents()[0].EventState != TriggerEvent::State::Stay) return -65;

            auto* pAgTr = lifeScene.TryGetComponent<TransformComponent>(agent);
            if (!pAgTr) return -66;
            pAgTr->Position = { 100.0f, 100.0f, 100.0f };
            psLife.Step(lifeScene, 1.0f / 60.0f);
            if (psLife.HasTriggerOverlap(trig, agent)) return -66;
            if (psLife.GetTriggerEvents().empty()) return -67;
            if (psLife.GetTriggerEvents()[0].EventState != TriggerEvent::State::Exit) return -68;
        }

        {
            u32 createdCount = 0;
            Entity lastCreatedEntity{};
            u64 lastCreatedID = 0;
            StringID lastCreatedTag{};
            auto tokCreated = EventBus::Subscribe<EntityCreatedEvent>([&](const EntityCreatedEvent& ev) {
                createdCount++;
                lastCreatedEntity = ev.entity;
                lastCreatedID = ev.entityID;
                lastCreatedTag = ev.tag;
            });

            u32 destroyedCount = 0;
            Entity lastDestroyedEntity{};
            u64 lastDestroyedID = 0;
            auto tokDestroyed = EventBus::Subscribe<EntityDestroyedEvent>([&](const EntityDestroyedEvent& ev) {
                destroyedCount++;
                lastDestroyedEntity = ev.entity;
                lastDestroyedID = ev.entityID;
            });

            Scene evScene;
            Entity testEnt = evScene.CreateEntity("EventTest");
            if (createdCount != 1 || lastCreatedEntity != testEnt || lastCreatedID != testEnt.GetIndex() || lastCreatedTag != StringID("EventTest")) return -80;

            evScene.DestroyEntity(testEnt);
            if (destroyedCount != 1 || lastDestroyedEntity != testEnt || lastDestroyedID != testEnt.GetIndex()) return -81;

            EventBus::Unsubscribe<EntityCreatedEvent>(tokCreated);
            EventBus::Unsubscribe<EntityDestroyedEvent>(tokDestroyed);

            u32 collisionEventCount = 0;
            f32 colImpulse = 0.0f;
            Entity lastColA{}, lastColB{};
            auto tokCol = EventBus::Subscribe<CollisionEvent>([&](const CollisionEvent& ev) {
                collisionEventCount++;
                colImpulse = ev.impulse;
                lastColA = ev.entityA;
                lastColB = ev.entityB;
            });

            u32 triggerEventCount = 0;
            TriggerEvent::State lastTriggerState = TriggerEvent::State::Exit;
            Entity lastTrigA{}, lastTrigB{};
            auto tokTrig = EventBus::Subscribe<PhysicsTriggerEvent>([&](const PhysicsTriggerEvent& ev) {
                triggerEventCount++;
                lastTriggerState = ev.state;
                lastTrigA = ev.triggerEntity;
                lastTrigB = ev.otherEntity;
            });

            Scene physScene;
            PhysicsSystem physSys;
            physScene.SetPhysicsSystem(&physSys);

            Entity b1 = physScene.CreateEntity("BodyA");
            auto& trA = physScene.AddComponent<TransformComponent>(b1);
            trA.Position = { -0.5f, 5.0f, 0.0f };
            auto& rbA = physScene.AddComponent<RigidBodyComponent>(b1);
            rbA.Type = BodyType::Dynamic;
            rbA.UseGravity = false;
            rbA.LinearVelocity = { 10.0f, 0.0f, 0.0f };
            auto& spA = physScene.AddComponent<SphereColliderComponent>(b1);
            spA.Radius = 1.0f;

            Entity b2 = physScene.CreateEntity("BodyB");
            auto& trB = physScene.AddComponent<TransformComponent>(b2);
            trB.Position = { 0.5f, 5.0f, 0.0f };
            auto& rbB = physScene.AddComponent<RigidBodyComponent>(b2);
            rbB.Type = BodyType::Dynamic;
            rbB.UseGravity = false;
            rbB.LinearVelocity = { -10.0f, 0.0f, 0.0f };
            auto& spB = physScene.AddComponent<SphereColliderComponent>(b2);
            spB.Radius = 1.0f;

            physSys.Step(physScene, 1.0f / 60.0f);
            if (collisionEventCount == 0 || colImpulse <= 0.0f || lastColA != b1 || lastColB != b2) return -82;

            Entity trigEnt = physScene.CreateEntity("TrigVol");
            auto& trT = physScene.AddComponent<TransformComponent>(trigEnt);
            trT.Position = { 20.0f, 0.0f, 0.0f };
            auto& boxT = physScene.AddComponent<BoxColliderComponent>(trigEnt);
            boxT.HalfSize = { 2.0f, 2.0f, 2.0f };
            boxT.IsTrigger = true;

            Entity visitor = physScene.CreateEntity("Visitor");
            auto& trV = physScene.AddComponent<TransformComponent>(visitor);
            trV.Position = { 20.0f, 0.0f, 0.0f };
            auto& rbV = physScene.AddComponent<RigidBodyComponent>(visitor);
            rbV.Type = BodyType::Dynamic;
            rbV.UseGravity = false;
            auto& boxV = physScene.AddComponent<BoxColliderComponent>(visitor);
            boxV.HalfSize = { 0.5f, 0.5f, 0.5f };

            physSys.Step(physScene, 1.0f / 60.0f);
            if (triggerEventCount == 0 || lastTriggerState != TriggerEvent::State::Enter || lastTrigA != trigEnt || lastTrigB != visitor) return -83;

            EventBus::Unsubscribe<CollisionEvent>(tokCol);
            EventBus::Unsubscribe<PhysicsTriggerEvent>(tokTrig);

            u32 deferredCount = 0;
            auto tokDef = EventBus::Subscribe<EntityCreatedEvent>([&](const EntityCreatedEvent&) {
                deferredCount++;
            });

            EventBus::Enqueue(EntityCreatedEvent{});
            EventBus::Enqueue(EntityCreatedEvent{});
            if (deferredCount != 0) return -84;

            EventBus::FlushDeferred();
            if (deferredCount != 2) return -85;

            EventBus::Unsubscribe<EntityCreatedEvent>(tokDef);

            bool luaSuccess = scriptEngine.ExecuteString(
                "_G.luaCreatedCount = 0\n"
                "_G.luaDestroyedCount = 0\n"
                "_G.luaCollisionCount = 0\n"
                "_G.luaTriggerCount = 0\n"
                "_G.luaCreatedId = -1\n"
                "_G.luaDestroyedId = -1\n"
                "_G.luaCollisionImpulse = 0\n"
                "_G.luaTriggerState = -1\n"
                "_G.luaTriggerStateStr = ''\n"
                "_G.luaEntityOOPWorks = false\n"
                "RL.Events.OnEntityCreated(function(e, id)\n"
                "    _G.luaCreatedCount = _G.luaCreatedCount + 1\n"
                "    _G.luaCreatedId = id\n"
                "    if e and e:IsValid() and e:GetIndex() == id then\n"
                "        _G.luaEntityOOPWorks = true\n"
                "    end\n"
                "end)\n"
                "RL.Events.OnEntityDestroyed(function(e, id)\n"
                "    _G.luaDestroyedCount = _G.luaDestroyedCount + 1\n"
                "    _G.luaDestroyedId = id\n"
                "end)\n"
                "RL.Events.OnCollision(function(a, b, imp, nx, ny, nz, px, py, pz)\n"
                "    _G.luaCollisionCount = _G.luaCollisionCount + 1\n"
                "    _G.luaCollisionImpulse = imp\n"
                "end)\n"
                "RL.Events.OnTrigger(function(t, o, st, str)\n"
                "    _G.luaTriggerCount = _G.luaTriggerCount + 1\n"
                "    _G.luaTriggerState = st\n"
                "    _G.luaTriggerStateStr = str\n"
                "end)\n"
            );
            if (!luaSuccess) return -86;

            Entity luaEnt = scene.CreateEntity("LuaEventEntity");
            const u32 expectedLuaEntIndex = luaEnt.GetIndex();
            scene.DestroyEntity(luaEnt);
            EventBus::Publish(CollisionEvent{ Entity(10), Entity(20), { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 2.5f });
            EventBus::Publish(PhysicsTriggerEvent{ Entity(30), Entity(40), TriggerEvent::State::Enter });

            lua_State* L = scriptEngine.GetLuaState();
            lua_getglobal(L, "luaCreatedCount");
            const int luaC = static_cast<int>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getglobal(L, "luaDestroyedCount");
            const int luaD = static_cast<int>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getglobal(L, "luaCollisionCount");
            const int luaCol = static_cast<int>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getglobal(L, "luaTriggerCount");
            const int luaTrig = static_cast<int>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getglobal(L, "luaCreatedId");
            const int luaCId = static_cast<int>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getglobal(L, "luaDestroyedId");
            const int luaDId = static_cast<int>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getglobal(L, "luaCollisionImpulse");
            const double luaImp = lua_tonumber(L, -1);
            lua_pop(L, 1);

            lua_getglobal(L, "luaTriggerState");
            const int luaTrigSt = static_cast<int>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getglobal(L, "luaEntityOOPWorks");
            const bool luaOOP = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);

            if (luaC < 1 || luaD < 1 || luaCol < 1 || luaTrig < 1) return -87;
            if (luaCId != static_cast<int>(expectedLuaEntIndex) || luaDId != static_cast<int>(expectedLuaEntIndex)) return -87;
            if (luaImp < 2.4 || luaImp > 2.6 || luaTrigSt != 0 || !luaOOP) return -87;

            bool recursiveHandled = scriptEngine.ExecuteString(
                "_G.recursiveCount = 0\n"
                "RL.Events.OnEntityCreated(function(e, id)\n"
                "    _G.recursiveCount = _G.recursiveCount + 1\n"
                "    if _G.recursiveCount <= 12 then\n"
                "        RL.Scene.CreateEntity('RecChild')\n"
                "    end\n"
                "end)\n"
            );
            if (!recursiveHandled) return -86;
            Entity recEnt = scene.CreateEntity("RecRoot");
            scene.DestroyEntity(recEnt);

            bool reloadFired = false;
            std::string reloadedPath;
            auto tokReload = EventBus::Subscribe<ScriptReloadEvent>([&](const ScriptReloadEvent& ev) {
                reloadFired = true;
                reloadedPath = ev.scriptPath;
            });

            if (!scriptEngine.ReloadScript()) return -88;
            if (!reloadFired || reloadedPath.empty()) return -89;

            EventBus::Unsubscribe<ScriptReloadEvent>(tokReload);

            scene.Clear();
        }

        scene.GetCamera().SetProjectionType(CameraProjection::Perspective);
        Entity cubeEnt = scene.CreateEntity("Cube");
        scene.AddComponent<MeshComponent>(cubeEnt, "Cube");

        Entity spotEnt = scene.CreateEntity("SmokeSpotLight");
        auto& spotTr = scene.GetComponent<TransformComponent>(spotEnt);
        spotTr.Position = { 2.0f, 4.0f, -2.0f };
        spotTr.Rotation = { 0.6f, 0.0f, 0.0f };
        auto& spotLc = scene.AddComponent<LightComponent>(spotEnt);
        spotLc.Type = LightType::Spot;
        spotLc.SpotAngle = 35.0f;
        spotLc.Intensity = 6.0f;
        spotLc.Range = 25.0f;

        if (!scriptEngine.ExecuteString(
                "local s = RL.Scene.CreateEntity('LuaAnimTest')\n"
                "s:PlayAnimation('RunAnim', 1.5)\n"
                "local playing1 = s:IsAnimationPlaying()\n"
                "s:SetAnimationSpeed(2.25)\n"
                "s:StopAnimation()\n"
                "local playing2 = s:IsAnimationPlaying()\n"
                "_G.luaAnimOk = (playing1 == true and playing2 == false)\n"))
        {
            return -97;
        }
        lua_State* animL = scriptEngine.GetLuaState();
        lua_getglobal(animL, "luaAnimOk");
        const bool luaAnimOk = lua_toboolean(animL, -1) != 0;
        lua_pop(animL, 1);
        if (!luaAnimOk) return -98;
    }

    Timer& timer = window.GetTimer();
    timer.Reset();

    f32 titleUpdateTimer = 0.0f;
    RL_LOG_INFO("All subsystems initialized successfully. Entering engine main loop...");

    while (true)
    {
        Input::NewFrame();
        if (Input::IsKeyPressed(KeyCode::F5))
        {
            renderSystem.RequestShaderReload();
        }
        EventBus::DispatchDeferred();
        AssetManager::PollHotReload();
        AssetManager::PollAsyncLoads();
        AudioEngine::Get().Update();

        if (!window.ProcessMessages() || window.ShouldClose())
        {
            break;
        }

        timer.Tick();
        const f32 dt = timer.GetDeltaTime();

        if (window.IsMinimized())
        {
            Sleep(10);
            continue;
        }

        if (editorLayer.GetSceneBuilder().IsSimulationRunning())
        {
            scriptEngine.Tick(dt);
            scene.Update(dt);
            stressTest.Update(scene, dt);
        }

        titleUpdateTimer += dt;
        if (titleUpdateTimer >= 0.25f)
        {
            titleUpdateTimer = 0.0f;
            const auto title = std::format(
                "RLengine 7.2 | GPU: {} | {}x{} | {} FPS ({:.2f} ms) | ECS Entities: {} (Rendered: {})",
                device.GetGpuDescription(),
                window.GetWidth(),
                window.GetHeight(),
                timer.GetFPS(),
                timer.GetFrameTimeMS(),
                scene.GetAliveEntityCount(),
                renderSystem.GetLastRenderedCount()
            );
            window.SetTitle(title);
        }

        editorLayer.BeginFrame();
        editorLayer.RenderUI(timer, device, scene, renderSystem.GetLastRenderedCount(), &renderSystem, &textureManager, &scriptEngine);

        device.BeginFrame();

        const auto& dx12Sky = scene.GetEnvironment().SkyColor;
        device.ClearRenderTarget(dx12Sky.x, dx12Sky.y, dx12Sky.z, dx12Sky.w);

        ID3D12GraphicsCommandList* cmdList = device.GetCommandList();

        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissorRect);

        renderSystem.SetViewport(viewport, scissorRect);
        renderSystem.SetMainRenderTarget(device.GetRTVHandle(), device.GetDSVHandle());

        const u32 currentFrameIndex = device.GetCurrentFrameIndex();
        scene.Render(cmdList, currentFrameIndex);

        editorLayer.EndFrame(cmdList);

        device.EndFrame();

        const bool vsync = editorLayer.GetSceneBuilder().IsVSyncEnabled();
        device.Present(vsync);

        if (smokeTest && timer.GetFrameCount() >= maxTestFrames)
        {
            const usize originalCount = scene.GetAliveEntityCount();
            const std::string jsonPath = (std::filesystem::temp_directory_path() / "rlengine_smoke_test.json").string();
            const std::string binPath = (std::filesystem::temp_directory_path() / "rlengine_smoke_test.bin").string();
            SceneSerializer::Save(scene, jsonPath);
            SceneSerializer::Save(scene, binPath);
            Scene jsonScene;
            SceneSerializer::Load(jsonScene, device.GetDevice(), jsonPath);
            Scene binScene;
            SceneSerializer::Load(binScene, device.GetDevice(), binPath);
            std::error_code ec;
            std::filesystem::remove(jsonPath, ec);
            std::filesystem::remove(binPath, ec);
            if (jsonScene.GetAliveEntityCount() != originalCount || binScene.GetAliveEntityCount() != originalCount)
            {
                RL_LOG_FATAL("Smoke test entity count mismatch: original {}, json {}, bin {}",
                    originalCount, jsonScene.GetAliveEntityCount(), binScene.GetAliveEntityCount());
                return -12;
            }
            RL_LOG_INFO("Smoke test completed successfully ({} frames rendered, JSON & Binary scene serialization verified, entity count verified: {}, rendered count: {}).",
                timer.GetFrameCount(), originalCount, renderSystem.GetLastRenderedCount());
            break;
        }
    }

    RL_LOG_INFO("Exiting engine main loop. Performing clean shutdown...");

    device.WaitForGPU();

    editorLayer.Shutdown();
    AssetManager::Shutdown();
    AudioEngine::Get().Shutdown();
    renderSystem.Destroy();
    scriptEngine.Shutdown();
    textureManager.Shutdown();
    descriptorManager.Shutdown();
    device.Shutdown();
    window.Shutdown();

    RL_LOG_INFO("RLengine 7.2 shutdown completed successfully.");
    return 0;
}
