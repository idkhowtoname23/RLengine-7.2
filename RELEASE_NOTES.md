# 🚀 RLengine 7.2 (`v7.2.0`) — Final Release Notes

**RLengine 7.2 (`v7.2.0`)** is the final production milestone of the 7.2 series, unifying all capabilities built across Alpha 1 through Beta into a verified C++23 dual-RHI 3D game engine and editor.

---

## 🌟 Complete Feature Summary (7.2 vs 7.1)

### 1. 🖥️ Dual RHI Rendering, Post-Processing & Winlator / DX11 Parity
* **DirectX 12 (`D3D_FEATURE_LEVEL_12_1`, SM 6.5 DXIL)**: Cook-Torrance GGX PBR, Tangent-space Normal Mapping (TBN), multi-layer glTF PBR extraction, 4-Cascade Shadow Mapping (2048x2048, 3x3 PCF), up to 16 dynamic Point/Spot lights (`DynamicLightData`), `WorldEnvironment` exponential fog & sky, `ViewportShadingMode` (`Wireframe`, `Solid`, `MaterialPreview`, `Rendered`), and **offscreen `R16G16B16A16_FLOAT` HDR Post-Processing** (9-tap Bloom, Exposure, ACES, Vignette).
* **DirectX 11 (`D3D_FEATURE_LEVEL_11_0`, SM 5.0 DXBC)**: Full parity for `DX11MeshCache` (`Cube`, `Sphere`, `Plane`, `Grid`, `Quad`, glTF meshes), directional (2048x2048) and **Point/Spot (1024x1024) PCF shadows**, dynamic `SkinnedMeshComponent` vertex deformation, `ParticleEmitterComponent` billboards, fog, and `Exposure`/ACES/`Vignette` post-processing.

### 2. ⚙️ Physics, Joints, Capsules & Ragdolls
* **15-Axis SAT OBB, Sphere & `CapsuleColliderComponent`**: Full 3D linear & angular impulse solver, swept-sphere & capsule ground CCD, Trigger volumes (`Enter`, `Stay`, `Exit`), resting jitter damping, and `PhysicsSystem::Raycast()`.
* **Mechanical Constraints & Ragdoll**: `DistanceJointComponent`, `HingeJointComponent` (limits & motors), `FixedJointComponent` (`BreakForce`), and `PhysicsSystem::SpawnRagdollChain()`.

### 3. 🦴 Skeletal Animation, Cross-Fade Blending & 2-Bone IK
* **`GLTFLoader` & `AnimationSystem`**: Full skeletal hierarchy, TRS keyframe LERP/SLERP, `CrossFade` pose blending, pause-time timeline scrubbing, **analytic Law-of-Cosines 2-Bone IK (`EnableIK`, `IKTargetPos`, `IKWeight`)**, and GPU `BonePalette` upload (up to 128 bones).

### 4. 📜 Lua 5.4 Scripting, 3D Audio & Async Asset Pipeline
* **Isolated Per-Entity `ScriptComponent`**: Per-entity `_ENV` tables supporting `OnStart(self)`, `OnUpdate(self, dt)`, `OnCollision(self, other)`, `OnTriggerEnter(self, other)`, plus OOP methods (`PlayAnimation`, `CrossFadeAnimation`, `SetIKTarget`, `PlaySound`, `ApplyImpulse`) and `RL.Physics.Raycast`.
* **Live Hot-Reloading & `ThreadPool` Streaming**: Overlapped `FileWatcher` HLSL hot-reloading (`F5`), concrete `AssetManager` importers (`MeshAsset`, `TextureAsset`, `ScriptAsset`), and `AssetManager::LoadAsync<T>()` + `PollAsyncLoads()`.

### 5. 🎛️ Blender-Style Editor, Visual Profiler & Standalone Game Export
* **DCC Workspace**: MMB Orbit / Shift+MMB Pan / Scroll Zoom / RMB+WASD Fly, Workspace Presets (`Default`, `Modeling`, `Animation`, `Scripting`, `Debug`), Viewport Drag-and-Drop (`RL_ASSET_PATH`), **Visual Frame Profiler (`FrameProfiler` + `StatsPanel`)**, and Binary v5 / JSON serialization & Undo/Redo.
* **Standalone Game Export & Runtime (`--play` / `game.rlconfig`)**: `File -> Export Game...` packages a self-contained runnable game directory that boots directly into standalone runtime mode on double-click.

---

## 🛠️ Final Verification Results

```powershell
.\RLengine7.2.exe --smoke-test
.\RLengine7.2.exe --smoke-test --dx11
.\RLengine7.2.exe --play --smoke-test 5
.\RLengine7.2.exe --stress-test 50000 --smoke-test 10
```
All suites exit with code `0`, and `0` code comments exist across `source/` and `shaders/`.
