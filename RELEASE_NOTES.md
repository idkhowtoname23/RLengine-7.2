# 🚀 RLengine 7.2 Alpha 2 — Pre-Release Notes

RLengine 7.2 Alpha 2 marks the second major milestone in the 7.2 development series. Building directly upon the foundation of physical accuracy, material fidelity, and editor isolation established in Alpha 1, Alpha 2 introduces mechanical constraints and joint simulation, background non-blocking live shader hot-reloading, spatial 3D audio with an object-oriented Lua bridge, and full multi-layer glTF PBR material autoloading.

---

## 🌟 Highlights & Major New Capabilities in Alpha 2

### 1. ⚙️ Physics Constraints & Joints Solver
* **DistanceJointComponent**:
  * Constrains distance between two entities or between an entity and a fixed world anchor.
  * Configurable stiffness and damping parameters with Baumgarte stabilization.
  * `AutoDistance` auto-calibration: automatically locks current distance on initialization.
  * Balanced mass-ratio correction: distributes displacement proportionally between dynamic bodies or applies 100% full correction against static/kinematic anchors.
* **HingeJointComponent**:
  * Enforces rotational movement constrained to a single 3D axis with anchor coincidence.
  * Eliminates angular velocity components perpendicular to the hinge axis.
  * Rotational limits: `LowerAngleLimit` and `UpperAngleLimit` bounds preventing over-rotation.
  * Powered motor support: `MotorSpeed` and `MaxMotorTorque` for motorized levers, wheels, and machinery.
* **FixedJointComponent**:
  * Rigidly locks relative position and orientation between two bodies.
  * Configurable `BreakForce`: calculates the cumulative impulse required to maintain the lock over $\Delta t$; if force exceeds `BreakForce`, the bond breaks (`IsBroken = true`) and fires a `PhysicsTriggerEvent` via the `EventBus`.

### 2. 🔥 Live Background Shader Hot-Reloading
* **Overlapped FileWatcher**:
  * Background directory watcher monitoring `shaders/` for `.hlsl` modifications.
  * Implemented using non-blocking Windows Overlapped I/O (`ReadDirectoryChangesW` + `CancelIoEx`), preventing thread deadlocks and allowing instantaneous engine shutdown.
* **Atomic Pipeline Swapping**:
  * `ReloadPipelines()` recompiles shaders and rebuilds Graphics Pipeline State Objects (Mesh 3D, Quad, Shadow Depth, Ground Grid) on the fly.
  * Triggered automatically upon saving shader files or manually via the `F5` hotkey.
* **Graceful Syntax Error Resilience**:
  * In the event of a HLSL compilation syntax error, the compiler logs the detailed DXC diagnostic output and preserves existing active pipelines. Rendering continues uninterrupted without crashes.

### 3. 🔊 3D Spatial Audio Subsystem & Lua OOP Bridge
* **Listener & Emitter Integration**:
  * `AudioListenerComponent` queries camera / entity 3D orientation and world position.
  * `AudioSourceComponent` syncs world emitter coordinates with miniaudio 3D distance attenuation.
* **Object-Oriented Lua 5.4 Scripting API**:
  * Entity instances expose intuitive audio controls:
    * `entity:PlaySound(soundPath, [volume], [pitch], [loop])`
    * `entity:StopSound()`
    * `entity:IsSoundPlaying()`
    * `entity:SetAudioVolume(volume)`
  * Global positional audio trigger: `RL.Audio.Play3D(soundPath, x, y, z, [volume])`.

### 4. 🎨 Multi-Layer glTF PBR Material Autoloading
* **Automated Texture Extraction**:
  * `GLTFLoader` now extracts normal maps (`normal_texture`), metallic-roughness maps (`metallic_roughness_texture`), and emissive maps (`emissive_texture`).
  * Scalar factors (`metallic_factor`, `roughness_factor`, `emissive_factor`) are bound directly into the scene's `MaterialComponent` upon model import.

---

## 🔍 Differences: 7.2 Alpha 2 vs 7.2 Alpha 1

| Feature / Subsystem | RLengine 7.2 Alpha 1 | RLengine 7.2 Alpha 2 (New) |
|---|---|---|
| **Mechanical Joints** | None (rigid dynamic collisions and triggers only) | **DistanceJoint**, **HingeJoint** (with limits & motor), **FixedJoint** (with `BreakForce`) |
| **Shader Compilation** | One-time compilation at startup; restart required | **Live Background Hot-Reloading** (`FileWatcher` + atomic PSO swap + `F5` keybind) |
| **Shader Error Handling** | Fatal exit or startup failure on corrupt bytecode | **Fault-Tolerant Fallback**: retains working pipelines if recompile fails |
| **Audio Scripting** | Flat global audio playback | **Full 3D Spatial Audio ECS** + `entity:PlaySound()`, `entity:StopSound()`, `RL.Audio.Play3D()` |
| **glTF Material Import** | Single diffuse / albedo texture binding | **Multi-Layer PBR Autoloading**: Normal, Metallic-Roughness, Emissive maps and factors |

---

## 📊 Comprehensive Comparison: 7.2 Alpha 2 vs 7.1

| Component | RLengine 7.1 | RLengine 7.2 Alpha 2 |
|---|---|---|
| **Collision Resolution** | Discrete 60Hz AABB solver | **Swept-Sphere CCD + 15-Axis SAT OBB** |
| **Joint Constraints** | None | **DistanceJoint, HingeJoint, FixedJoint** |
| **Sensor / Trigger Volumes** | None (impulse separation only) | **Trigger Colliders (`IsTrigger`) with lifecycle events** |
| **Material Pipeline** | Basic Albedo, Roughness, Metallic | **Full Tangent PBR (TBN) + Multi-Layer glTF (Normal, Metallic-Roughness, Emissive)** |
| **Shader Pipeline** | Startup compilation only | **Live Overlapped Hot-Reloading with F5 keybind & atomic PSO swap** |
| **Audio Subsystem** | Basic flat playback | **3D Spatial Positional Audio + Lua OOP entity methods** |
| **Event Architecture** | Direct tightly-coupled function calls | **Type-Erased EventBus with `DepthGuard` recursion guard** |
| **Editor Ergonomics** | Monolithic overlapping windows | **Blender 4.x DCC Station + Floating Non-Dockable REPL Console** |
| **Undo / Redo** | Basic transform stack | **Deep Entity Cloning & Component Snapshot Undo/Redo** |
| **Hardware Telemetry** | Log-based output | **Real-Time `MemoryStats` (Working Set & Private RAM via Win32 PSAPI)** |

---

## 🛠️ Verification & Smoke Testing

All builds are validated using automated headless smoke tests:

```powershell
# Automated DirectX 12 Smoke Test (15 frames + physics + joints + serialization)
.\RLengine7.2.exe --smoke-test

# Automated DirectX 11 Fallback Smoke Test (Legacy / Winlator compatibility)
.\RLengine7.2.exe --smoke-test --dx11
```

Both test suites exit with code `0`.
