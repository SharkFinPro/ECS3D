# AGENTS.md - ECS3DRender

Agents load this file when they read a file in `source/libs/render/`. It holds what a change here must keep. The full
notes are in the ECS3DRender section of
[docs/architecture/library-reference.md](../../../docs/architecture/library-reference.md).

Related design notes: [camera-and-input.md](../../../docs/architecture/camera-and-input.md),
[viewport-gizmos.md](../../../docs/architecture/viewport-gizmos.md).

- The server does not link this library. Logic worth testing goes in a headless header (`Billboard.h`,
  `CapsuleGizmo.h`, `HullGizmoMesh.h`, `SharedAssetLoads.h`, `GridVisibility.h`, `LightGizmoVisibility.h`,
  `OutlineColors.h`) that the test suite compiles without Vulkan.
- The engine's loaders throw rather than return null. `GpuAssetCache` loads through `SharedAssetLoads.h`, which logs a
  failed load once and remembers it, so `getModel`, `getTexture` and `getRenderObject` return null instead. Every
  caller tolerates null, so a missing asset file shows nothing instead of ending the app.
- `vke::raii` owns Vulkan handles. Release GPU objects through the engine's deferred release, which waits for the last
  frame that could use them; a project root change applies that way in `RenderSystem::variableUpdate`.
- `setProjectionParameters` throws for a degenerate fov/near/far triple. `RenderSystem` pushes the projection only when
  it changes, and catches and logs that throw once.
