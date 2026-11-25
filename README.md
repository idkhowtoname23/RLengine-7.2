# RLengine 7.2 Alpha 5 (Pre-Release)

> **Release Stage**: 🧪 **Alpha (Alpha 5 - Pre-Release)**  
> **Build Target**: C++23 | Visual Studio 18 2026 / 2022 | DirectX 12 (FL 12_1) & DirectX 11 (FL 11_0)  
> **Code Quality**: Strict Zero Comments Policy enforced across all source, header, shader, script, and build configuration files.

---

## 📌 Overview & Development Status

**RLengine 7.2 Alpha 5** represents milestone 5 of the planned 5–7 alpha phases towards the December 2026 final RLengine 7.2 release. Building directly upon Alpha 4's HDR post-processing, animation cross-fading, per-entity Lua scripting, and async asset streaming, Alpha 5 delivers **capsule collider physics & articulated ragdoll chains**, **`RL.Physics.Raycast` & `entity:ApplyImpulse` Lua APIs**, **DirectX 11 hardware parity for skeletal skinning, particle billboards, and Exposure/ACES/Vignette post-processing**, **Blender-style Workspace Presets (`Default`, `Modeling`, `Animation`, `Scripting`, `Debug`) & Viewport Drag-and-Drop**, **Binary v4 serialization**, and **Standalone Game Runtime (`--play`)**.

All automated smoke tests pass cleanly with code `0` across both **DirectX 12 (Shader Model 6.5 DXIL)** and **DirectX 11 (Shader Model 5.0 DXBC)** pipelines.

---

## 🔍 Differences: 7.2 Alpha 3 vs Alpha 2 vs Alpha 1 vs 7.1

### 1. Key Evolution: 7.2 Alpha 5 vs 7.2 Alpha 3

| Architectural Subsystem | RLengine 7.2 Alpha 3 | RLengine 7.2 Alpha 4 & Alpha 5 (New) | Impact & Improvements |
| :--- | :--- | :--- | :--- |
| **Post-Processing** | Tonemapping in mesh shader only | **HDR `R16G16B16A16_FLOAT` Target + `postprocess.hlsl` + DX11 PostFX** | 9-tap soft Bloom, Exposure, ACES filmic curve, and Vignette controlled via `WorldEnvironment`. |
| **Skeletal Animation** | Single-clip FK `BonePalette` | **Cross-Fade Clip Blending + Timeline Scrubbing + DX11 Skinning** | `CrossFadeAnimation()`, `XMQuaternionSlerp` pose transitions, pause scrubbing, and Winlator/DX11 skinning parity. |
| **Physics & Lua API** | Box/Sphere colliders & Joints | **`CapsuleColliderComponent`, `SpawnRagdollChain`, `RL.Physics.Raycast`, `ApplyImpulse`** | Capsule collision/trigger solver, 4-segment articulated ragdoll chains, and Lua raycast queries. |
| **Scripting & Assets** | Global `main.lua`, sync loaders | **Per-Entity `_ENV` `ScriptComponent` + `ThreadPool` `LoadAsync<T>`** | Isolated `OnStart`/`OnUpdate`/`OnCollision`/`OnTriggerEnter` per entity plus background decoding. |
| **Editor & Runtime** | Fixed Blender layout | **Workspace Presets + Viewport Drag-and-Drop + `--play` Standalone Mode** | `Default/Modeling/Animation/Scripting/Debug` layouts, drag assets from Content Browser, and headless-editor `--play <scene>`. |

### 2. Comprehensive Comparison: 7.2 Alpha 3 vs 7.1

| Component | RLengine 7.1 | RLengine 7.2 Alpha 3 | Evolution & Benefits |
| :--- | :--- | :--- | :--- |
| **Collision & Constraints** | Discrete 60Hz AABB solver | **Swept-Sphere CCD + 15-Axis SAT OBB + Joints** | Anti-tunneling CCD, 3D angular impulses, Triggers, and Distance/Hinge/Fixed joints with `BreakForce` & serialization. |
| **Animation Pipeline** | Static transforms only | **glTF Hierarchical Skinning + `AnimationSystem`** | Keyframe TRS animation channels, FK bone matrices, GPU `BonePalette` upload, and Lua playback controls. |
| **Lighting & Atmosphere** | Single directional/point light | **4-Cascade CSM + 16 Dynamic Point/Spot Lights + Fog** | Dynamic light array (`DynamicLightData`), Spot cone falloff, exponential sky fog, and live viewport shading modes. |
| **DX11 Legacy Backend** | Cube-only basic pass | **Full Multi-Mesh (`DX11MeshCache`), Shadows, Fog & Quads** | Procedural & glTF mesh parity, PCF directional shadows, billboards, wireframe mode, and multi-light SM 5.0 shading. |
| **Asset & Shader Workflow** | Compile/load once at startup | **Live Overlapped Shader Reload + Concrete `AssetManager`** | Atomic HLSL GPSO swapping (`F5`) plus `MeshAsset`, `TextureAsset`, `ScriptAsset` importers with hot-reload queue. |
| **Audio Subsystem** | Basic flat playback | **3D Spatial Positional Audio + Lua OOP** | Distance-attenuated 3D emitters, listener orientation tracking, and `entity:PlaySound()` Lua methods. |
| **Event Bus Architecture** | Tight direct calls | **Type-Erased EventBus + `DepthGuard`** | Decoupled publish-subscribe messaging; Lua scripting bridge with recursive cascade protection. |
| **Editor Ergonomics** | Monolithic overlapping windows | **Blender 4.x DCC Station + MMB Navigation + Floating REPL** | Dockable panels, MMB Orbit / Shift+MMB Pan / Scroll Zoom, snap toggle, AABB picking, and complete component inspectors. |
| **Hardware Telemetry** | Basic logs | **Real-Time `MemoryStats` (PSAPI)** | Live tracking of Working Set RAM, Private Committed Bytes, and subsystem allocations. |

---

## 🚀 Deep Dive: Major New Features in 7.2 Alpha 2

### 1. ⚙️ Physics Constraints & Joints Solver
* **DistanceJointComponent**: Constrains relative distance between anchors with configurable stiffness and damping. Features `AutoDistance` auto-calibration and dynamic/static mass ratio correction.
* **HingeJointComponent**: Enforces anchor coincidence, projects angular velocity onto the hinge axis, and supports angular limits (`LowerAngleLimit`, `UpperAngleLimit`) and powered motors (`MotorSpeed`, `MaxMotorTorque`).
* **FixedJointComponent**: Rigidly locks relative position and orientation, with configurable `BreakForce` threshold publishing `PhysicsTriggerEvent` when excessive forces break the bond.

### 2. 🔥 Live Shader Hot-Reloading
* **Overlapped FileWatcher**: Background directory watcher monitoring `shaders/` using non-blocking Windows overlapped I/O (`ReadDirectoryChangesW` + `CancelIoEx`) without blocking engine shutdown.
* **Atomic Pipeline Swapping**: `ReloadPipelines()` recompiles modified vertex/pixel shaders and recreates Graphics Pipeline State Objects (Mesh, Quad, Shadow Depth, Ground Grid) with graceful fallback to existing pipelines on compilation error.
* **Uniform VFS Resolution**: Seamless resolution across `shaders://` paths, relative local directories, and output binaries.

### 3. 🔊 3D Spatial Audio Subsystem & Lua OOP Bridge
* **Listener & Emitter Tracking**: `AudioListenerComponent` queries camera/entity orientation while `AudioSourceComponent` syncs position, volume, and pitch with miniaudio 3D attenuation.
* **Lua 5.4 Object-Oriented Methods**: Script entities expose `entity:PlaySound()`, `entity:StopSound()`, `entity:IsSoundPlaying()`, and `entity:SetAudioVolume()`, complemented by global `RL.Audio.Play3D()`.

### 4. 🎨 Multi-Layer glTF PBR Autoloading
* **Comprehensive PBR Extraction**: `GLTFLoader` automatically extracts normal maps, metallic-roughness textures, emissive maps, metallic factors, roughness factors, and emissive colors, binding them directly to `MaterialComponent` upon scene import.

### 1. ⚡ Continuous Collision Detection (CCD) & Trigger Zones
* **High-Speed Anti-Tunneling**: Discrete physics engines suffer from tunneling when high-velocity objects travel further than collider thicknesses in a single frame. 7.2 Alpha 1 introduces ray-cast swept sphere continuous collision detection (`m_ccdEnabled = true`, `SetCCDEnabled`). Dynamic rigidbodies predict motion paths and clamp time-of-impact (TOI) against geometry before position integration, preventing penetration up to 900+ units/sec.
* **Non-Physical Triggers**: Both `BoxColliderComponent` and `SphereColliderComponent` now feature an `IsTrigger` toggle. Triggers detect overlap and dispatch lifecycle events (`PhysicsTriggerEvent`) to the EventBus without applying physical collision impulses.

### 2. 🪐 15-Axis Separating Axis Theorem (SAT) OBB Physics
* **Arbitrary 3D Rotations**: Version 7.1 approximated oriented collisions through axis-aligned bounding boxes (AABB). Version 7.2 Alpha 1 implements the full 15-axis Separating Axis Theorem:
  * 3 face normals of Box A ($A_0, A_1, A_2$)
  * 3 face normals of Box B ($B_0, B_1, B_2$)
  * 9 cross products of box edge directions ($A_i \times B_j$)
* **Exact Contact Manifolds**: Computes minimal penetration depths, contact normals, and contact points, feeding an angular impulse solver ($\Delta \omega = I^{-1}(r \times J)$) with accurate 3D inertia tensors.

### 3. 🎨 Tangent-Space PBR Normal Mapping (TBN Frame)
* **Vertex Tangent Stream**: Vertex buffers (`MeshVertex`) are extended with 16-byte aligned `DirectX::XMFLOAT4 Tangent` vectors (with handness sign in `w`).
* **Procedural & glTF Tangents**: `GLTFLoader` and procedural primitives (Cube, Sphere, Cylinder, Plane, Grid) compute orthonormal Gram-Schmidt tangents and bitangents, preventing degenerate UVs and shader NaNs.
* **HLSL Normal Perturbation (`mesh3d.hlsl`)**: Reconstructs the orthonormal $TBN = [T, B, N]$ matrix on the GPU, transforms sampled tangent-space normal vectors into world-space, and applies Cook-Torrance GGX microfacet specular reflections with ACES tonemapping.
* **DirectX 11 Fallback**: Includes flat normal fallback (`0xFFFF8080`, tangent $\{0, 0, 1\}$) when normal textures are omitted.

### 4. 📡 Unified EventBus with Lua `RL.Events` & `DepthGuard`
* **Decoupled Communication**: Centralized publish-subscribe message broker allowing engine systems and scripts to communicate without direct dependencies.
* **Subscribed Events**: `EntityCreatedEvent`, `EntityDestroyedEvent`, `CollisionEvent`, `PhysicsTriggerEvent`, `WindowResizedEvent`, `ScriptReloadEvent`.
* **Lua 5.4 Scripting API**: Scripts register callbacks via `RL.Events.Subscribe("Collision", function(e) ... end)`.
* **Stack Overflow Prevention**: The `DepthGuard` recursion guard (depth limit: 8) detects and aborts re-entrant event cascades when script callbacks trigger chained entity creations or scene modifications.

### 5. 🎛️ Isolated Blender 4.x DCC Editor & Ergonomics
* **Dedicated Window Zones**: The 3D Viewport, Left Tool Shelf, Right Outliner (Scene Hierarchy), Right Properties (Inspector), and Bottom Timeline occupy dedicated, non-overlapping screen spaces.
* **Safe Overlay Padding**: Diagnostic text (`User Orthographic / Perspective`, Camera Coordinates, FPS) dynamically offsets from the Tool Shelf to guarantee zero visual collisions.
* **Floating Non-Dockable REPL Console**: The in-game Lua console (`~` or `F1`) is equipped with `ImGuiWindowFlags_NoDocking`, ensuring it acts as a floating terminal and cannot accidentally split or corrupt viewport docking nodes.
* **Reset Layout Shortcut**: `Window ➔ Reset Layout to Default` in the main menu bar allows one-click recovery of the standard Blender 4.x workspace layout.

---

## 🏗️ Building and Verification

### Prerequisites
* **OS**: Windows 10 / 11 (x64)
* **Compiler**: Visual Studio 2026 or 2022 (MSVC `/std:c++23`)
* **Build System**: CMake 3.25+
* **SDK**: Windows SDK with DirectX 12 and DirectX 11 support

### Build Commands
```powershell
# Configure CMake
cmake -B build -G "Visual Studio 18 2026" -A x64

# Build Release binary
cmake --build build --config Release --parallel
```

### Automated Verification Smoke Tests
```powershell
# Automated DirectX 12 Smoke Test (15 frames + physics + serialization verification)
.\build\bin\Release\RLengine7.2.exe --smoke-test

# Automated DirectX 11 Fallback Smoke Test
.\build\bin\Release\RLengine7.2.exe --smoke-test --dx11
```

---

## 📦 Pre-Release Archive Contents

The pre-release binary archive (`RLengine-7.2-Alpha-5-Windows-x64.zip`) contains:
* `RLengine7.2.exe`: Optimized Release executable.
* `dxcompiler.dll`, `dxil.dll`: Official Microsoft DirectX Shader Compiler runtime.
* `shaders/`: DX12 and DX11 HLSL shaders (`mesh3d.hlsl`, `quad.hlsl`, `grid3d.hlsl`, `shadow_depth.hlsl`).
* `scripts/`: Lua 5.4 script files (`main.lua`).
* `assets/`: Procedural 3D models, textures, branding, and default scene files.
* `imgui.ini`: Clean editor workspace layout.

---

## 📜 License
Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.
