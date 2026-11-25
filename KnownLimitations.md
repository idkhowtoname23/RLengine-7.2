# Known Limitations (RLengine 7.2)

This document provides a transparent overview of deliberate architectural boundaries and remaining backlog items in **RLengine 7.2 (Alpha 5)** ahead of the final release.

---

### 1. DirectX 11 Fallback Scope
* **Implemented through 7.2 Alpha 5**: Multi-mesh GPU vertex/index caching (`Cube`, `Sphere`, `Plane`, `Grid`, `Quad`, glTF meshes), `QuadRenderComponent` alpha-blended billboards, `Wireframe` rasterizer mode, `MaterialComponent` mapping, multi-light (Point & Spot) attenuation, `WorldEnvironment` sky/ambient/fog, **dynamic `SkinnedMeshComponent` deformation**, **`ParticleEmitterComponent` camera-facing billboards**, **`Exposure`/ACES/`Vignette` SM 5.0 post-processing**, and 2048x2048 PCF directional shadow mapping.
* **Planned for Future**: Point light shadow maps and multi-cascade shadows on legacy DirectX 11 hardware.

---

### 2. Rigid Body Physics, Collisions & Constraints
* **Implemented through 7.2 Alpha 5**: Deterministic 60Hz solver with 3D spatial hash broadphase, 15-axis SAT OBB vs OBB, Sphere, and **`CapsuleColliderComponent`** (Capsule vs Plane, Box, Sphere, Capsule, and Ray-Capsule intersection), Swept-Sphere CCD, Triggers (`Enter`, `Stay`, `Exit`), **Joints (`DistanceJoint`, `HingeJoint`, `FixedJoint`)**, **articulated `SpawnRagdollChain`**, and Lua `RL.Physics.Raycast` + `entity:ApplyImpulse`.
* **Planned for Future**: Soft-body / cloth simulation and continuous swept capsule-vs-mesh collisions.

---

### 3. Skeletal Animation & Skinning Pipeline
* **Implemented through 7.2 Alpha 5**: Full glTF skeletal hierarchy & TRS keyframe import (`GLTFLoader`), forward kinematics interpolation (`AnimationSystem`), GPU `BonePalette` upload (up to 128 bones), DX11 skinning parity, cross-fade blending (`CrossFade`), timeline scrubbing while paused, `Loop` control, and Lua OOP playback controls.
* **Planned for Future**: Two-bone IK foot/hand placement and additive animation layers.

---

### 4. Editor Workspace Layout & Runtime Modes
* **Implemented through 7.2 Alpha 5**: Full dynamic docking (`ImGuiConfigFlags_DockingEnable`), **Workspace Presets (`Default`, `Modeling`, `Animation`, `Scripting`, `Debug`)** persisted in `imgui.ini`, **Viewport Drag-and-Drop asset instantiation (`RL_ASSET_PATH`)**, Blender-style 3D Viewport navigation, complete JSON & Binary v4 serialization/Undo, and **Standalone Runtime Mode (`--play <scene>`)**.
* **Planned for Future**: One-click standalone game export packaging wizard inside the editor UI.

---

### 5. Asset Pipeline & Architectural Subsystems
* **Implemented through 7.2 Alpha 5**: Real-time `MemoryStats`, unified `EventBus` with Lua bindings & `DepthGuard`, live overlapped HLSL shader hot-reloading (`F5`), concrete `AssetManager` importers (`MeshAsset`, `TextureAsset`, `ScriptAsset`), **`ThreadPool`-backed `AssetManager::LoadAsync<T>()`**, queued `PollHotReload()`, and **isolated per-entity `ScriptComponent` Lua `_ENV` runtime**.
* **Planned for Future**: Dynamic hot-reloading for custom compute shaders.
