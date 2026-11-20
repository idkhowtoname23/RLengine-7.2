# Known Limitations (RLengine 7.2)

This document provides a transparent overview of deliberate architectural boundaries and known limitations in **RLengine 7.2**. These items do not constitute release defects, but rather define the scope of the 7.1 release and establish the backlog for **RLengine 7.2**.

---

### 1. DirectX 11 Fallback Scope
* **Status**: Dedicated compatibility fallback targeting `D3D_FEATURE_LEVEL_11_0`.
* **Scope**: Renders textured and colored geometry, ground grid, hierarchical entity transforms, and directional shadow maps.
* **Implemented in 7.2**: Texture sampling (linear sampler, fallback 1x1 white texture, per-mesh diffuse texture binding) and directional shadow mapping (2048x2048 depth pass, comparison sampler, PCF shadow factor) are fully implemented for legacy hardware!
* **Planned for Future**: Point light shadow mapping and multi-cascade shadow maps for legacy hardware.

---

### 2. Rigid Body Physics, Collisions & Constraints
* **Status**: Deterministic 60Hz solver with 3D spatial hash grid broadphase, 3D Oriented Bounding Box (OBB vs OBB) collision detection via Separating Axis Theorem (SAT), 8-corner rotated Box vs Ground Plane ($Y=0$) contact resolution, Box vs Sphere pairwise dynamic collision response, ray-AABB, and ray-sphere testing.
* **Implemented in 7.2**: Full dynamic 3D rigid body contact resolution with linear and angular momentum resolution (impulse solver applying $\Delta v = J / m$ and $\Delta \omega = I^{-1}(r \times J)$ with box moment of inertia tensor approximation) across OBB vs OBB (15 separating axes), Box vs Plane ($Y=0$), Box vs Sphere, and Sphere vs Sphere. Continuous Collision Detection (CCD) against high-velocity tunneling, Trigger Collider support (`IsTrigger` with lifecycle event tracking `Enter`, `Stay`, `Exit`), and **joint constraints solver (`DistanceJointComponent`, `HingeJointComponent` with limits/motors, `FixedJointComponent` with `BreakForce`)** are fully implemented and verified!
* **Planned for Future**: Articulated multi-body ragdoll constraints and soft body physics.

---

### 3. Editor Workspace Layout
* **Status**: Blender 4.x inspired dark-themed interface with virtualized Outliner (`ImGuiListClipper`), 3D transform gizmos, and properties panels.
* **Implemented in 7.2**: Full dynamic docking support with detachable panels (`ImGuiConfigFlags_DockingEnable`, `ImGui::DockSpaceOverViewport`, and automatic layout persistence via `imgui.ini`) is fully integrated! All primary editor windows (SceneHierarchyPanel, InspectorPanel, StatsPanel, ContentBrowserPanel, LuaConsole, Viewport) dock cleanly into a customizable workspace that allows dynamic moving, undocking, floating, and tabbed grouping.
* **Planned for Future**: Custom workspace layout presets (Modeling, Scripting, Animation, Debug) with runtime layout import/export.

---

### 4. Undo / Redo Command History
* **Status**: Multi-level `CommandHistory` with keyboard shortcuts (`Ctrl+Z`, `Ctrl+Y`, `Ctrl+Shift+Z`).
* **Scope**: Records transformation changes (`Move`, `Rotate`, `Scale`), property changes, and full hierarchical entity lifecycle commands.
* **Implemented in 7.2**: Full undo/redo support for entity creation (default and custom configured components), deletion (with full entity component snapshots including Tag, Transform, Mesh, Material, Light, Camera, RigidBody, BoxCollider, SphereCollider, and Hierarchy parent/child relationships), duplication (deep entity cloning with unique UUID generation), and scene hierarchy reparenting (dynamic parent/child detachment and re-attachment)!
* **Planned for Future**: Multi-selection batch undo/redo and visual undo history tree graph.

---

### 5. Architectural Stubs
* **Status**: `EventBus`, `AssetManager`, `MemoryStats`, and `ServiceLocator` are integrated into the core architecture.
* **Memory Telemetry in 7.2**: `MemoryStats` is fully wired with real-time OS Process (working set RAM, private committed memory) via Windows PSAPI and subsystem telemetry across Graphics (instance buffers, constant buffers, cascade shadow maps), Textures (default white, loaded and color textures), and Scripts (Lua runtime GC allocations).
* **Unified EventBus in 7.2**: `EventBus` is fully wired as a centralized publish-subscribe message broker with immediate synchronous publishing and thread-safe deferred queueing (`FlushDeferred`/`DispatchDeferred`). Integrated with Scene lifecycle events (`EntityCreatedEvent`, `EntityDestroyedEvent`), deterministic physics solver events (`CollisionEvent`, `PhysicsTriggerEvent`), platform window events (`WindowResizedEvent`), script reload events (`ScriptReloadEvent`), and bidirectional Lua 5.4 script bindings (`RL.Events.OnEntityCreated`, `RL.Events.OnEntityDestroyed`, `RL.Events.OnCollision`, `RL.Events.OnTrigger`) with re-entrant call stack protection (`DepthGuard`)!
* **Shader Hot-Reloading in 7.2**: Dynamic shader hot-reloading is fully implemented! The background `FileWatcher` tracks file modifications and recreates Graphics Pipeline State Objects on the fly without restarting the engine.
* **Planned for Future**: AssetManager streaming pipeline and dynamic hot-reloading for custom compute shaders.
