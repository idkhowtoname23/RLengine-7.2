# Known Limitations (RLengine 7.2)

This document provides a transparent overview of deliberate architectural boundaries and known limitations in **RLengine 7.2 (Alpha 3)**. These items do not constitute release defects, but rather define the current milestone scope toward the final **RLengine 7.2** release.

---

### 1. DirectX 11 Fallback Scope
* **Status**: Dedicated compatibility fallback targeting `D3D_FEATURE_LEVEL_11_0` (Shader Model 5.0).
* **Implemented in 7.2 Alpha 3**: Multi-mesh GPU vertex/index caching (`Cube`, `Sphere`, `Plane`, `Grid`, `Quad`, and custom glTF meshes loaded via `MeshRef::GetCpuVertices()`), `QuadRenderComponent` alpha-blended 2D billboard rendering, `Wireframe` rasterizer state toggle, `MaterialComponent` parameter mapping, multi-light (Point & Spot light cone falloff) attenuation, `WorldEnvironment` sky clear color, ambient intensity, and exponential fog parity, plus 2048x2048 directional shadow mapping with PCF.
* **Planned for Future**: Point light cube/perspective shadow mapping, multi-cascade shadow maps, and hardware skeletal bone skinning parity on legacy DirectX 11 hardware.

---

### 2. Rigid Body Physics, Collisions & Constraints
* **Status**: Deterministic 60Hz solver with 3D spatial hash grid broadphase, 3D Oriented Bounding Box (OBB vs OBB) collision detection via Separating Axis Theorem (SAT), 8-corner rotated Box vs Ground Plane ($Y=0$) contact resolution, Box vs Sphere pairwise dynamic collision response, ray-AABB, and ray-sphere testing.
* **Implemented in 7.2**: Full dynamic 3D rigid body contact resolution with linear and angular momentum resolution across OBB vs OBB (15 separating axes), Box vs Plane ($Y=0$), Box vs Sphere, and Sphere vs Sphere. Continuous Collision Detection (CCD) against high-velocity tunneling, Trigger Collider support (`IsTrigger` with lifecycle event tracking `Enter`, `Stay`, `Exit`), and joint constraints solver (`DistanceJointComponent`, `HingeJointComponent` with limits/motors, `FixedJointComponent` with `BreakForce`) with full JSON & Binary scene serialization and Undo/Redo snapshot support.
* **Planned for Future**: Articulated multi-body ragdoll constraints and soft body physics.

---

### 3. Skeletal Animation & Skinning Pipeline
* **Implemented in 7.2 Alpha 4**: Full glTF skeletal hierarchy parsing (`GLTFLoader`), forward kinematics interpolation (`AnimationSystem`), `BonePalette` GPU upload (up to 128 bones), **cross-fade animation blending (`CrossFade`)**, timeline scrubbing while paused, `Loop` control, and Lua OOP playback controls (`PlayAnimation`, `CrossFadeAnimation`, `StopAnimation`, `SetAnimationSpeed`, `SetAnimationTime`, `GetAnimationTime`).
* **Planned for Future**: IK foot/hand placement and multi-track additive animation layers.

---

### 4. Editor Workspace Layout & Viewport Controls
* **Implemented in 7.2 Alpha 3**: Full dynamic docking (`ImGuiConfigFlags_DockingEnable`, `imgui.ini` persistence), Blender-style 3D Viewport navigation (MMB Orbit, Shift/Alt+MMB Pan, Scroll Zoom, RMB+WASD/QE Fly-Mode), functional Gizmo Snap toggle, fallback AABB ray-picking for procedural/custom entities without physics colliders, `ViewportShadingMode` (`Wireframe`, `Solid`, `MaterialPreview`, `Rendered`), `WorldEnvironment` controls, and Inspector UI for Joints, SkinnedMesh, ParticleSystem, AudioSource/Listener, and Point/Spot Lights.
* **Planned for Future**: Custom workspace layout presets (Modeling, Scripting, Animation, Debug) with runtime layout import/export.

---

### 5. Asset Pipeline & Architectural Subsystems
* **Implemented in 7.2**:
  * **Memory Telemetry (`MemoryStats`)**: Real-time Windows PSAPI process RAM and GPU/Lua telemetry.
  * **Unified `EventBus`**: Publish-subscribe messaging with deferred queueing and Lua callbacks protected by `DepthGuard`.
  * **Live Shader Hot-Reloading**: Background `FileWatcher` (Overlapped I/O) with atomic GPSO swapping.
  * **Async `AssetManager` Streaming & Importers (Alpha 4)**: Concrete `MeshAsset`, `TextureAsset`, and `ScriptAsset` importers (`AssetManager::RegisterDefaultImporters()`), `ThreadPool`-backed `AssetManager::LoadAsync<T>()` + `PollAsyncLoads()`, and thread-safe queued hot-reload notifications via `AssetManager::PollHotReload()`.
* **Planned for Future**: Dynamic hot-reloading for custom compute shaders.
