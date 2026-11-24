# 🚀 RLengine 7.2 Alpha 3 — Pre-Release Notes

RLengine 7.2 Alpha 3 marks the third major milestone (out of 5–7 planned alphas) toward the final RLengine 7.2 release. Building upon the physics constraints, spatial audio, and shader hot-reloading introduced in Alpha 2, Alpha 3 delivers end-to-end **skeletal animation & glTF skinning**, **multi-light DX12 shading (up to 16 dynamic lights + SpotLights + atmospheric fog)**, **DirectX 11 multi-mesh & environment parity**, **concrete `AssetManager` importers with thread-safe hot-reload queuing**, **full serialization/Undo support for Joints & `WorldEnvironment`**, and **Blender-style 3D viewport navigation**.

---

## 🌟 Highlights & Major New Capabilities in Alpha 3

### 1. 🦴 Skeletal Animation & glTF Keyframe Skinning
* **glTF Hierarchy & TRS Keyframes (`GLTFLoader`)**: Extracts node parent indices, rest local transforms, and full Translation/Rotation/Scale keyframe timestamps and value channels (`cgltf_animation_path_type_translation`, `_rotation`, `_scale`).
* **Forward Kinematics Runtime (`AnimationSystem`)**: Interpolates keyframes via `XMVectorLerp` and `XMQuaternionSlerp`, evaluates hierarchical bone transforms parent-before-child, updates `SkinnedMeshComponent::BonePalette` (up to 128 bones per entity), and provides `CreateProceduralTestRig` for procedural validation.
* **GPU Skinning Upload (`RenderSystem`)**: Uploads active `BonePalette` matrices directly to per-frame structured buffers consumed by `mesh3d.hlsl`.
* **Lua 5.4 OOP Animation Controls**: Exposes `entity:PlayAnimation(animName, [speed])`, `entity:StopAnimation()`, `entity:SetAnimationSpeed(speed)`, and `entity:IsAnimationPlaying()`.

### 2. 💡 Multi-Light DX12 Shading, SpotLights, Atmosphere & Viewport Modes
* **Up to 16 Dynamic Lights (`DynamicLightData` StructuredBuffer)**: Binds a dedicated 64-byte structured light array (`t1, space1`) alongside the primary directional shadow caster, evaluating multiple **Point** and **Spot** lights per pixel in `mesh3d.hlsl`.
* **SpotLight Cone Falloff**: `LightType::Spot` now computes smooth inner/outer cone angle attenuation alongside radial inverse-square attenuation.
* **World Environment & Fog**: `Scene::GetEnvironment()` stores `SkyColor`, `AmbientColor`, `AmbientIntensity`, `FogEnabled`, `FogDensity`, and `FogColor`, transmitted via `ObjectConstants` (`PassCB`) for exponential atmospheric fog and dynamic clear color across DX12 and DX11.
* **Viewport Shading Modes**: Wired `SceneShadingMode` (`Wireframe`, `Solid`, `MaterialPreview`, `Rendered`) with a dedicated DX12 `m_wireframePipeline` and matching shader modes.

### 3. 🖥️ DirectX 11 Parity & Concrete Asset Importers
* **DX11 Multi-Mesh & Feature Parity (`DX11Device`)**: Replaced single-cube geometry with `DX11MeshCache` supporting procedural `Cube`, `Sphere`, `Plane`, `Grid`, `Quad`, and custom glTF/CPU meshes (`Mesh::GetCpuVertices()`). Added `QuadRenderComponent` alpha-blended billboards, `Wireframe` rasterizer state, `WorldEnvironment` fog/ambient/sky clear color, `MaterialComponent` parameters, and Point/Spot light attenuation in SM 5.0.
* **Concrete `AssetManager` Importers (`AssetImporters.hpp`)**: Implemented `MeshAsset` (glTF via `cgltf`), `TextureAsset` (RGBA image decoding via `stb_image`), and `ScriptAsset` (Lua UTF-8 script source) registered via `AssetManager::RegisterDefaultImporters()`.
* **Deadlock-Free Asset Hot-Reload Queue**: Updated `AssetManager::PollHotReload()` so `FileWatcher` pushes modified paths onto a mutex-guarded queue drained safely on the main thread.

### 4. 🎛️ Blender-Style Viewport Controls, Complete Serialization & Inspector UI
* **JSON & Binary v2 Serialization + Undo/Redo**: `SceneSerializer` (`BINARY_VERSION = 2` with v1 backwards compatibility) now persists and restores `DistanceJointComponent`, `HingeJointComponent` (including cross-entity `TargetEntity` UUID resolution), `FixedJointComponent`, and `WorldEnvironment`. `CommandHistory::EntitySnapshot` captures and restores all three joint components on delete/undo/redo.
* **Blender-Style 3D Viewport Navigation**: Implemented MMB Orbit around focus pivot, Shift/Alt+MMB Pan, Scroll Zoom, and RMB+WASD/QE Fly navigation in `SceneBuilder`.
* **Viewport UX & Presets**: Enabled functional Gizmo snapping (`m_snapEnabled`), added fallback AABB ray-picking in `PickEntityAtScreenPos` for entities without physics colliders, synced `World`/`Shading` editor menus to live `Scene` state, added Inspector panels for Joints, SkinnedMesh, ParticleSystem, AudioSource/Listener, and fixed `BodyType` dropdown mapping (`Static`, `Dynamic`, `Kinematic`).

---

## 🔍 Differences: 7.2 Alpha 3 vs 7.2 Alpha 2

| Feature / Subsystem | RLengine 7.2 Alpha 2 | RLengine 7.2 Alpha 3 (New) |
|---|---|---|
| **Skeletal Animation** | Component struct stub only | **Full glTF hierarchy + TRS keyframes, `AnimationSystem` FK, GPU `BonePalette` upload & Lua API** |
| **Lighting & Atmosphere** | Single directional/point light, static clear color | **Up to 16 dynamic Point/Spot lights, Spot cone falloff, `WorldEnvironment` sky/ambient/fog** |
| **Viewport Shading** | Solid PBR mode only | **4 modes: `Wireframe`, `Solid`, `MaterialPreview`, `Rendered`** |
| **DX11 Geometry & Features** | Cube-only geometry | **`DX11MeshCache` (Cube/Sphere/Plane/Grid/Quad/glTF), Quads, Wireframe, Fog, Multi-Light** |
| **Asset Pipeline** | Abstract `AssetManager` cache only | **Concrete `MeshAsset`, `TextureAsset`, `ScriptAsset` importers + `PollHotReload()` queue** |
| **Serialization & Undo** | Core components only (missing joints/env) | **Binary v2 + JSON for `Distance/Hinge/FixedJoint` & `WorldEnvironment` + Undo snapshots** |
| **Viewport Controls** | WASD + RMB look only | **Blender MMB Orbit, Shift+MMB Pan, Scroll Zoom, Snap toggle & AABB picking** |

---

## 🛠️ Verification & Smoke Testing

All automated Release verification suites exit with code `0` and enforce the **Strict Zero Comments Policy** (`0` comments across `source/` and `shaders/`):

```powershell
# Automated DirectX 12 Smoke Test (AnimationSystem, AssetManager, Joints/WorldEnv serialization, 15 frames)
.\RLengine7.2.exe --smoke-test

# Automated DirectX 11 Fallback Smoke Test (DX11 multi-mesh cache + environment + 15 frames)
.\RLengine7.2.exe --smoke-test --dx11
```
