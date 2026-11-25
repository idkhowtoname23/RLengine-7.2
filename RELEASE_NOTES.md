# 🚀 RLengine 7.2 Alpha 4 — Pre-Release Notes

RLengine 7.2 Alpha 4 marks the fourth major milestone in the 7.2 development cycle. It introduces HDR offscreen post-processing, animation cross-fade blending, isolated per-entity Lua scripting, background thread-pool asset streaming, and Binary v3 scene serialization.

---

## 🌟 Major New Capabilities in Alpha 4

### 1. ✨ DX12 HDR Offscreen Post-Processing (`shaders/postprocess.hlsl`)
* **R16G16B16A16_FLOAT HDR Render Target**: `RenderSystem` renders the 3D scene, grid, and quads into a dedicated offscreen HDR buffer (automatically resized with the viewport) and resolves to the backbuffer via a fullscreen triangle pass.
* **Bloom, Exposure & Vignette**: Configurable via `WorldEnvironment` (`PostProcessEnabled`, `Exposure`, `BloomThreshold`, `BloomIntensity`, `VignetteIntensity`) with 9-tap soft bloom, ACES filmic tonemapping, sRGB gamma, and live `F5` shader hot-reload support.

### 2. 🦴 Animation Cross-Fade Blending & Timeline Scrubbing
* **Smooth Clip Transitions (`AnimationSystem`)**: `SkinnedMeshComponent::CrossFade(animName, duration)` blends local bone TRS (`XMVectorLerp` for translation/scale, shortest-path `XMQuaternionSlerp` for rotation) between `PreviousAnimation` and `CurrentAnimation` before computing FK `BonePalette` matrices.
* **Timeline Scrubbing & Looping**: `AnimationSystem` evaluates `BonePalette` even when paused (`IsPlaying == false`) for instant inspector timeline scrubbing, and respects `SkinnedMeshComponent::Loop`.
* **Lua OOP Methods**: Added `entity:CrossFadeAnimation(animName, [duration], [speed])`, `entity:SetAnimationTime(t)`, and `entity:GetAnimationTime()`.

### 3. 📜 Per-Entity `ScriptComponent` Lua 5.4 Runtime
* **Isolated `_ENV` Sandboxes (`ScriptEngine`)**: Each entity with an enabled `ScriptComponent` runs in its own Lua `_ENV` table inheriting `_G`, supporting `OnStart(self)`, `OnUpdate(self, dt)`, `OnCollision(self, other)`, and `OnTriggerEnter(self, other)` without global symbol collisions.
* **Inspector & Hot-Reload**: `InspectorPanel` adds `ScriptPath`, `Enabled`, and `Reload Script` controls alongside `WorldEnvironment` Post-Process and `SkinnedMeshComponent` scrubbing/cross-fade controls.

### 4. ⚡ Async Background `AssetManager` Streaming
* **`ThreadPool`-Backed `LoadAsync<T>`**: Decodes `MeshAsset` (glTF) and `TextureAsset` (`stb_image`) on worker threads without stalling the main thread, finalizing cache registration and callbacks safely in `AssetManager::PollAsyncLoads()`.
* **Telemetry & Drag-Drop**: `StatsPanel` displays live cached and pending async asset counts; `ContentBrowserPanel` exposes `"RL_ASSET_PATH"` drag-and-drop payloads.

### 5. 💾 Binary v3 & JSON Complete Serialization
* **`SceneSerializer` (`BINARY_VERSION = 3`)**: Full backwards-compatible persistence for `ParticleEmitterComponent`, `AudioSourceComponent`, `AudioListenerComponent`, `SkinnedMeshComponent::Loop`, and `WorldEnvironment` post-processing settings.

---

## 🛠️ Verification & Smoke Testing

```powershell
.\RLengine7.2.exe --smoke-test
.\RLengine7.2.exe --smoke-test --dx11
```
Both DX12 and DX11 suites pass with exit code `0` and `0` code comments across `source/` and `shaders/`.
