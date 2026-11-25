# 🚀 RLengine 7.2 Alpha 4 & Alpha 5 — Pre-Release Notes

**RLengine 7.2 Alpha 5** completes both Phase A (Alpha 4) and Phase B (Alpha 5) toward the December 2026 final release, delivering HDR post-processing, animation cross-fade blending, per-entity Lua scripting, async asset streaming, capsule physics & ragdoll chains, DirectX 11 skinning/particle parity, editor workspace presets, and standalone game runtime (`--play`).

---

## 🌟 Highlights of Alpha 5

### 1. 🦴 Capsule Colliders, Articulated Ragdoll Chains & Lua Physics API
* **`CapsuleColliderComponent` Solver**: `PhysicsSystem` now resolves Capsule-vs-Plane ($Y=0$), Capsule-vs-Capsule, Capsule-vs-Sphere, and Capsule-vs-Box contacts and trigger events (`IsTrigger`), plus exact segment/endcap ray-capsule intersections in `PhysicsSystem::Raycast()`.
* **Articulated Ragdoll Preset**: Added `PhysicsSystem::SpawnRagdollChain(scene, origin)` spawning a 4-segment linked capsule rig with `DistanceJoint` and `HingeJoint` constraints, exposed under `Add -> Physics -> Physics Ragdoll Chain` and `Shift+A`.
* **Lua 5.4 Physics Bindings**: Added `RL.Physics.Raycast(ox, oy, oz, dx, dy, dz, [maxDist])` returning `(hit, entity, hx, hy, hz, nx, ny, nz, distance)` and `entity:ApplyImpulse(x, y, z)`.
* **Serialization v4 & Undo**: `SceneSerializer` (`BINARY_VERSION = 4`), `CommandHistory::EntitySnapshot`, and `InspectorPanel` support `CapsuleColliderComponent`.

### 2. 🖥️ DirectX 11 Skinning, Particles & PostFX Parity (Winlator / Legacy GPUs)
* **DX11 Skeletal Skinning**: `DX11Device` deforms `SkinnedMeshComponent` vertices against `BonePalette` into a dynamic vertex buffer for both shadow and main passes.
* **DX11 Particle Billboards**: Simulates and renders `ParticleEmitterComponent` emitters using alpha-blended camera-facing `Quad` billboards with color/size interpolation and floor bounce.
* **DX11 Post-Processing Parity**: SM 5.0 pixel shader applies `WorldEnvironment` `Exposure`, ACES tonemapping, and radial screen-space `VignetteIntensity`.

### 3. 🎛️ Workspace Presets & Viewport Asset Drag-and-Drop
* **Workspace Presets (`Window -> Workspace Presets`)**: Added `EditorLayer::ApplyWorkspacePreset` (`Default`, `Modeling`, `Animation`, `Scripting`, `Debug`) powered by `ImGui::DockBuilder`, persisted in `imgui.ini` via `RLWorkspace` settings handler.
* **Viewport Drag-and-Drop**: Dragging `.gltf`/`.glb`, `.lua`, `.png`/`.jpg`/`.tga`, or `.wav`/`.mp3`/`.ogg` assets from `ContentBrowserPanel` (`"RL_ASSET_PATH"`) onto the 3D Viewport imports models or assigns scripts, textures, and audio sources directly to entities.

### 4. 🎮 Standalone Runtime (`--play <scene>`)
* Launches RLengine directly into full simulation (`ScriptComponent`, `PhysicsSystem`, `AnimationSystem`, `AudioSystem`) without editor UI panels across both DirectX 12 and DirectX 11.

---

## ✨ Highlights from Alpha 4 (Included)
* **DX12 Offscreen HDR Post-Processing (`shaders/postprocess.hlsl`)**: `DXGI_FORMAT_R16G16B16A16_FLOAT` offscreen target with 9-tap Bloom, Exposure, ACES filmic curve, and Vignette.
* **Animation Cross-Fade Blending & Timeline Scrubbing**: `entity:CrossFadeAnimation(name, duration, [speed])`, `SetAnimationTime()`, `GetAnimationTime()`, and pause-time pose updates.
* **Isolated Per-Entity `ScriptComponent` Runtime**: Per-entity Lua `_ENV` tables supporting `OnStart(self)`, `OnUpdate(self, dt)`, `OnCollision(self, other)`, and `OnTriggerEnter(self, other)`.
* **Async Background Asset Streaming**: `ThreadPool`-backed `AssetManager::LoadAsync<T>()` + main-thread `PollAsyncLoads()` + live asset telemetry in `StatsPanel`.

---

## 🛠️ Verification & Smoke Testing

```powershell
.\RLengine7.2.exe --smoke-test
.\RLengine7.2.exe --smoke-test --dx11
.\RLengine7.2.exe --play --smoke-test 5
```
All suites pass cleanly with exit code `0` and `0` code comments across `source/` and `shaders/`.
