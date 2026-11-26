# Known Limitations (RLengine 7.2)

This document provides a transparent overview of deliberate architectural boundaries and remaining backlog items in **RLengine 7.2 (Beta / Release Candidate)** ahead of `v7.2.0` Final and `RLengine 8.0`.

---

### 1. DirectX 11 Fallback Scope
* **Implemented through 7.2 Beta**: Multi-mesh GPU vertex/index caching (`Cube`, `Sphere`, `Plane`, `Grid`, `Quad`, glTF meshes), `QuadRenderComponent` alpha-blended billboards, `Wireframe` rasterizer mode, `MaterialComponent` mapping, multi-light attenuation, `WorldEnvironment` sky/ambient/fog, dynamic `SkinnedMeshComponent` deformation, `ParticleEmitterComponent` billboards, `Exposure`/ACES/`Vignette` SM 5.0 post-processing, 2048x2048 PCF directional shadows, and **1024x1024 Point/Spot PCF shadows**.
* **Planned for 8.0**: Native Vulkan 1.3 RHI backend (`DX12` + `Vulkan 1.3` + `DX11` fallback) and multi-cascade shadows on legacy hardware.

---

### 2. Rigid Body Physics, Collisions & Constraints
* **Implemented through 7.2 Beta**: Deterministic 60Hz solver with 3D spatial hash broadphase, 15-axis SAT OBB vs OBB, Sphere, `CapsuleColliderComponent` (with swept ground CCD and resting jitter damping), Swept-Sphere CCD, Triggers (`Enter`, `Stay`, `Exit`), Joints (`DistanceJoint`, `HingeJoint`, `FixedJoint` with angular clamp), articulated `SpawnRagdollChain`, and Lua `RL.Physics.Raycast` + `entity:ApplyImpulse`.
* **Planned for 8.0**: Convex Hull / Triangle Mesh GJK+EPA colliders, Character Controller, and NavMesh AI pathfinding.

---

### 3. Skeletal Animation & Skinning Pipeline
* **Implemented through 7.2 Beta**: Full glTF skeletal hierarchy & TRS keyframe import (`GLTFLoader`), forward kinematics (`AnimationSystem`), **analytic 2-Bone IK solver (`EnableIK`, `SetIKTarget`)**, GPU `BonePalette` upload (up to 128 bones), DX11 skinning parity, cross-fade blending (`CrossFade`), timeline scrubbing while paused, `Loop` control, and Lua OOP playback controls.
* **Planned for 8.0**: Additive animation layers and state-machine graph editor.

---

### 4. Editor Workspace Layout, Profiler & Export
* **Implemented through 7.2 Beta**: Full dynamic docking (`ImGuiConfigFlags_DockingEnable`), Workspace Presets (`Default`, `Modeling`, `Animation`, `Scripting`, `Debug`), Viewport Drag-and-Drop (`RL_ASSET_PATH`), Blender-style 3D Viewport navigation, **Visual Frame Profiler (`FrameProfiler` + `StatsPanel`)**, JSON & Binary v5 serialization/Undo, **Standalone Runtime (`--play` / `game.rlconfig`)**, and **One-Click Game Export Wizard (`File -> Export Game...`)**.
* **Planned for 8.0**: Node-based Material Graph editor and `.rlpak` asset bundle compression.

---

### 5. Asset Pipeline & Scripting
* **Implemented through 7.2 Beta**: Real-time `MemoryStats`, unified `EventBus` with Lua bindings & `DepthGuard`, live overlapped HLSL shader hot-reloading (`F5`), concrete `AssetManager` importers, `ThreadPool`-backed `AssetManager::LoadAsync<T>()`, and isolated per-entity `ScriptComponent` Lua `_ENV` runtime.
* **Planned for 8.0**: **Rust C-ABI Scripting Bridge (`rlengine-rs`)** with hot-reloadable Rust `cdylib` gameplay modules alongside Lua 5.4.
