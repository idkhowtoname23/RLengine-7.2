# 🚀 RLengine 7.2 Beta — Release Candidate Notes

**RLengine 7.2 Beta** completes the feature set of the 7.2 series ahead of the December 2026 final release, adding standalone game export, DX11 Point/Spot shadow mapping, analytic 2-Bone IK, capsule/ragdoll stability, and a built-in visual frame profiler.

---

## 🌟 New Capabilities in 7.2 Beta

### 1. 📦 One-Click Standalone Game Export (`GameExporter.hpp`)
* **Editor Export Wizard (`File -> Export Game...`)**: Packages a self-contained game folder with a renamed executable, required DirectX compiler/runtime DLLs, `shaders/`, `scripts/`, `assets/`, the active scene (`assets/scenes/main.rlscene`), and `game.rlconfig`.
* **Automatic `game.rlconfig` Boot**: When launched next to `game.rlconfig`, RLengine automatically starts in standalone Play Mode (`playMode = true`) without requiring `--play` CLI flags.

### 2. 💡 DirectX 11 Point/Spot Light Shadow Mapping & Optimization
* **1024x1024 Perspective Depth Pass (`t2`, `s1`)**: `DX11Device` renders dynamic `CastShadows` Point/Spot light depth into `m_pointShadowDSV` and applies 3x3 PCF shadow attenuation in SM 5.0.
* **Buffer Reuse**: `UpdateSkinnedVertexBuffer` reuses `m_skinnedVertexBuffer` when capacity suffices, and particle simulation reuses persistent scratch vectors with zero per-frame heap churn.

### 3. 🦴 Analytic 2-Bone IK & Physics Stability
* **Law-of-Cosines 2-Bone IK (`AnimationSystem`)**: Evaluates IK after FK pose blending when `SkinnedMeshComponent::EnableIK` is enabled, rotating `IKRootBone` and `IKMidBone` toward `IKTargetPos` blended by `IKWeight` and updating `BonePalette` descendants.
* **Lua API & Serialization v5**: Added `entity:SetIKTarget(enabled, x, y, z, [weight])`, `InspectorPanel` IK controls, and `SceneSerializer` `BINARY_VERSION = 5` (backwards compatible with v1–v4).
* **Capsule CCD & Ragdoll Stability (`PhysicsSystem`)**: Swept capsule bottom-endpoint CCD against `Y = 0` (preserving horizontal sliding on ground), grounded micro-jitter velocity damping, and joint angular velocity clamping.

### 4. 📊 Visual Frame Profiler (`Core/Profiler.hpp`)
* **`FrameProfiler` & `ScopedProfileTimer`**: Records per-subsystem millisecond timings (`Physics`, `Animation`, `Particles`, `Audio`, `Render`) with a 120-frame rolling history.
* **`StatsPanel` Graphs**: Displays live progress bars and `ImGui::PlotLines` frame-time graphs inside the editor.

---

## 🛠️ Verification

```powershell
.\RLengine7.2.exe --smoke-test
.\RLengine7.2.exe --smoke-test --dx11
.\RLengine7.2.exe --play --smoke-test 5
```
All suites pass with exit code `0` and `0` code comments across `source/` and `shaders/`.
