# Viewport Transform Gizmos — Findings & Plan

Goal: manipulate the selected object's transform directly in the scene view by clicking and
dragging, with hotkeys and/or toolbar buttons to switch between translate / rotate / scale modes —
instead of (in addition to) typing values into the Inspector's Transform editor.

This document covers the current architecture, the recommended approach, and the concrete changes
needed in both ECS3D and VulkanEngine.

---

## 1. How the relevant pieces work today

### The edit path (ECS3D)

The editor is a **network client**: the authoritative scene lives on the spawned `--edit` server,
and every edit is a command sent back over the wire. A transform edit currently flows:

```
TransformEditor (ImGui drag boxes, libs/editor/components/TransformEditor.cpp)
  └─ mutates the local replicated Transform (instant visual feedback)
  └─ returns edited = true
ObjectInspector (libs/editor/ObjectInspector.cpp:319)
  └─ fires the edit callback wired in EditorApp's ctor
EditorApp (apps/editor/EditorApp.cpp, `editComponent` lambda)
  └─ replication::buildComponentEdit(objectUUID, component) → NetClient::send
Server (apps/server/ServerApp.cpp, handleEditComponent)
  └─ applies the edit, echoes it to other connected views
```

A gizmo is just a **second producer of exactly this edit**: mutate the local `Transform`, then send
`buildComponentEdit`. Nothing new is needed in the protocol, the server, or replication.

Useful properties of the existing data model:

- `Transform` (libs/data/objects/components/Transform.h) stores **local** position / rotation
  (euler degrees) / scale. Parent combination is simple: position adds, rotation adds, scale
  multiplies — there is no full matrix hierarchy (a parent's rotation does not orbit its
  children). This makes world→local conversion for a gizmo trivial:
  `localPos = worldPos - parentWorldPos`, `localRot = worldRot - parentWorldRot`,
  `localScale = worldScale / parentWorldScale`.
- `ComponentVariable` transparently routes `set()` to the live value while the sim is running and
  to the initial (serialized) value while stopped — the gizmo inherits the same live/initial
  semantics the Inspector already has, for free.
- `EditorSelection` (libs/editor/Selection.h) is the single shared selection slot;
  `EditorApp::handlePicking` already writes it from Ctrl+click GPU picking, so "which object gets
  the gizmo" is already solved.
- `m_serverEditable` already gates all mutating UI; the gizmo must respect it too.

### The viewport (VulkanEngine)

- The scene view is an **engine-owned ImGui window** (`RenderingManager::renderGuiScene`,
  source/components/renderingManager/RenderingManager.cpp): the 3D scene is rendered offscreen and
  drawn as an `ImGui::Image` inside a dockable window whose name (`sceneViewName`) the editor
  already knows (`m_sceneViewName`).
- The ImGui context is **shared** between engine and editor (`ImGuiInstance::getImGuiContext()`;
  the editor already draws all its panels into the same frame). So the editor *can* draw an overlay
  on top of the scene image — that is exactly how an ImGui-based gizmo works.
- The viewport's screen position and extent are tracked every frame — but only pushed into
  `MousePicker` (`setViewportPos` / `setViewportExtent`); there are **no public getters**.
- The **view matrix** lives in `Renderer3D::m_viewMatrix` (private, no getter). Both camera modes
  funnel through `Renderer3D::setCameraParameters` — the engine free-fly camera in
  `VulkanEngine::render()`, and ECS3D's component cameras via `RenderSystem::updateCamera` — so one
  getter covers both.
- The **projection matrix** is not owned by anyone: it is lazily built inside
  `RenderInfo::getProjectionMatrix()` (source/components/pipelines/GraphicsPipeline.h) with a
  hardcoded 45° fov / 0.1 near / 1000 far, aspect from the current extent (and duplicated again in
  RayTracer.cpp). It is not reachable from outside the render pass. **This is the main engine
  gap** — gizmo math needs view + projection + viewport rect.
- GPU mouse picking (`MousePicker`) already exists and the editor uses it (Ctrl+click → per-object
  `bool*` pick results read through `RenderSystem::isSelected`).
- `Renderer3D::renderLine(start, end)` exists — usable for a hand-rolled gizmo, but see below for
  why that's not the recommended route.
- Input: `Window` exposes `keyIsPressed` / `buttonIsPressed` / `getCursorPos`, plus a
  `KeyCallbackEvent` the editor already subscribes to (`EditorApp::setupKeybinds`, currently F10).
- The free-fly camera consumes **W A S D, Space, LShift (whenever the scene view is focused) and
  right-mouse drag** for looking. This constrains hotkey choices (see §4).

---

## 2. Recommended approach: ImGuizmo

Use [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) (MIT, one .cpp/.h pair, by an ImGui
maintainer-adjacent author; it is the de-facto standard for ImGui editors). It draws
translate/rotate/scale handles into an ImGui draw list and handles all the picking, axis/plane
dragging, and screen-space math internally. Its inputs are exactly:

- view matrix, projection matrix (column-major float16 — glm works directly)
- the object's model matrix (it hands back the manipulated matrix)
- the viewport rectangle (`ImGuizmo::SetRect`)
- current operation (TRANSLATE / ROTATE / SCALE) and space (WORLD / LOCAL)

That means the whole feature reduces to: *expose camera matrices + viewport rect from VulkanEngine,
then wire ImGuizmo into the editor next to the existing edit callback.*

Why not hand-roll with `renderLine` + `MousePicker`? It would require building ray-from-screen
math, handle geometry, per-handle hit tests, depth-independent rendering, and all drag states —
several times the code for a worse result. ImGuizmo also renders as a UI overlay (always on top of
the scene image), which is what you want for gizmos.

One Vulkan-specific gotcha: the engine flips the projection's Y (`projectionMatrix[1][1] *= -1`)
for Vulkan clip space. ImGuizmo expects a GL-style projection, so the matrix handed to the editor
(or to ImGuizmo) must be the **un-flipped** one.

---

## 3. Changes needed in VulkanEngine

1. **Expose camera parameters.** Add getters on `Renderer3D` (or a small
   `CameraParameters { glm::vec3 position; glm::mat4 view; glm::mat4 projection; }` returned from
   one call):
   - `getViewMatrix()` / `getViewPosition()` — trivial, the fields already exist
     (`m_viewMatrix`, `m_viewPosition`).
   - `getProjectionMatrix()` — requires centralizing projection creation first. Today it is built
     ad hoc inside `RenderInfo` (and again in `RayTracer`). Move fov/near/far into one place (e.g.
     `Renderer3D`, fed by the current offscreen viewport extent, ideally configurable through
     `EngineConfig`), have `RenderInfo`/`RayTracer` consume it, and expose the GL-style (pre-flip)
     matrix publicly. This is a cleanup the engine arguably wants anyway.

2. **Expose the scene-view viewport rect.** `RenderingManager` already computes the ImGui-space
   position and extent every frame in `renderGuiScene` (it pushes them into `MousePicker`). Store
   them and add getters, e.g. `RenderingManager::getViewportPos()` / `getViewportExtent()`. (The
   no-dockspace path should report `{0,0}` + swapchain extent, which it already computes.)

3. **Build ImGuizmo alongside ImGui.** The engine owns the ImGui build
   (source/cmake/External.cmake compiles imgui into a static `imgui` target). ImGuizmo must compile
   against that same imgui, so fetch it there and add `ImGuizmo.cpp` to the `imgui` target (or a
   sibling `imguizmo` target linking `imgui`). ECS3D's editor lib then gets the header through the
   include dirs it already inherits.
   - `ImGuizmo::BeginFrame()` must run once per frame right after `ImGui::NewFrame()`. The engine
     owns the frame boundary (`ImGuiInstance::createNewFrame`), so calling it there is the robust
     spot (a one-line dependency); alternatively the editor can call it at the top of `updateGui()`.

4. **Optional quality-of-life: gate free-fly movement behind right-mouse.** Unity-style
   W/E/R mode hotkeys collide with the free-fly camera's WASD (pressing W to switch to translate
   would also fly the camera forward). Options:
   - engine change: only apply WASD movement while the right mouse button is held (common editor
     behavior, and it matches how the camera already gates looking on right-drag); or
   - no engine change: ECS3D picks non-conflicting hotkeys (see §4).

Nothing else engine-side: drawing happens through the shared ImGui context, and the edit path is
pure ECS3D.

---

## 4. Changes needed in ECS3D

1. **New editor-lib class — `libs/editor/TransformGizmo.{h,cpp}`** (part of ECS3DEditorLib):
   - Holds the current mode (`None | Translate | Rotate | Scale`) and, later, space
     (world/local) and snapping increments.
   - Per frame, given the selected object + camera params + viewport rect:
     - compose the model matrix from the object's **world** transform
       (`glm::translate * eulerRotation * scale`, matching `RenderObject`'s convention);
     - `ImGui::Begin(sceneViewName)` (appends into the engine's existing scene-view window) or
       `ImGuizmo::SetDrawlist` + `SetRect(viewportPos, viewportExtent)`;
     - `ImGuizmo::Manipulate(view, projection, op, mode, model)`;
     - if manipulated: decompose (`ImGuizmo::DecomposeMatrixToComponents` gives
       translation/euler-degrees/scale — the same representation `Transform` stores), convert
       world→local using the parent's world transform (simple subtract/divide, per §1), call
       `transform->setPosition/setRotation/setScale`, and report "edited" to the caller.
   - Expose `isUsing()` / `isOver()` (thin wrappers over ImGuizmo) so the app can suppress
     conflicting input.

2. **Wire it in `EditorApp`:**
   - Create it next to the other panels; call it from `updateGui()` after the panels, only when
     `m_selection->objectUUID()` resolves to an object with a `Transform`, and only when
     `m_serverEditable` is true.
   - On "edited", send the same message the Inspector sends:
     `m_netClient->send(replication::buildComponentEdit(objectUUID, transform))`. Sending every
     frame during a drag matches the Inspector's existing behavior (it fires per changed frame);
     if that proves chatty, throttle to N Hz during drag + one final send on release.
   - **Picking interaction:** in `handlePicking()`, skip selection changes while
     `gizmo.isUsing() || gizmo.isOver()` so grabbing a handle can't deselect the object.
     (Ctrl+click picking is mostly safe already, but a plain-click-drag gizmo means the guard is
     needed once the modifier is dropped or if handles overlap another object.)
   - **Delta clobbering:** while the sim is running the server streams `stateDelta`s; an incoming
     delta can overwrite the transform mid-drag between sends. Simplest fix: while
     `gizmo.isUsing()`, skip applying the Transform portion of deltas for the selected object (or
     re-apply the drag value after `handleStateDelta`). Dragging while paused/stopped — the common
     editing case — has no such conflict.

3. **Mode hotkeys** — extend `setupKeybinds()`'s existing `KeyCallbackEvent` listener:
   - Only react when the scene view is focused (`m_renderer->getRenderingManager()->isSceneFocused()`)
     and ImGui doesn't want the keyboard (`!ImGui::GetIO().WantCaptureKeyboard`), so typing in a
     panel never switches modes.
   - Key choice: **W/E/R (Unity-style) conflicts with the free-fly camera's WASD** unless the
     engine gates movement behind right-mouse (§3.4). Conflict-free alternatives with no engine
     change: `1/2/3` (+ `Q` or `Esc` for none) or Blender-style `G/R/S`. Recommendation: do the
     engine right-mouse gate and use Q/W/E/R — it's the convention most users expect, and the
     camera gate is independently a UX win.

4. **Toolbar buttons** — a small button group (Select / Move / Rotate / Scale, showing the active
   mode) either in the existing **Scene Status** top panel next to the play controls, or as a
   floating overlay in the scene-view corner (an `ImGui::Begin` with no decoration positioned over
   the viewport). Scene Status is the low-effort, consistent spot; the overlay looks more like a
   traditional editor. Either way the buttons just set `TransformGizmo`'s mode.

5. **CMake:** nothing beyond what VulkanEngine provides (ImGuizmo header comes through the
   inherited include dirs). If ImGuizmo were fetched by ECS3D instead, it would have to compile
   against the engine's imgui target — doable but messier; prefer the engine-side build.

---

## 5. Suggested implementation order

1. **VulkanEngine:** centralize the projection matrix; add `Renderer3D` view/projection getters and
   `RenderingManager` viewport-rect getters; fetch + build ImGuizmo; call `ImGuizmo::BeginFrame()`
   in `ImGuiInstance::createNewFrame`. (Engine builds and runs identically — all additive except
   the projection refactor.)
2. **ECS3D:** `TransformGizmo` with translate-only, hardcoded mode, world space, no hierarchy —
   prove the drag → local mutate → `buildComponentEdit` loop end to end.
3. Add rotate/scale ops, world→local conversion for child objects, and the `isUsing()` guards
   (picking suppression, delta skip).
4. Hotkeys + toolbar UI + (optional) the engine's right-mouse camera gate.
5. Polish: snapping (ImGuizmo supports it natively), local/world space toggle, hiding the gizmo
   while looking through a scene camera if it feels wrong there.

## 6. Out of scope / future notes

- **Undo:** there is no undo system anywhere in the edit path. Gizmo drags should eventually
  coalesce into a single undoable edit (capture value on drag-start, one entry on release) — worth
  keeping in mind in `TransformGizmo`'s API shape, but it's not needed to ship this.
- **Multi-select:** `EditorSelection` is a single slot; the gizmo is single-object by design.
- **Colliders:** `BoxCollider`/`SphereCollider` local offsets already render as debug shapes
  (`RenderSystem::variableUpdate`); a later iteration could point the same gizmo at collider
  local transforms.
