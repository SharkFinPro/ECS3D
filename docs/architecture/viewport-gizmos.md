# Viewport gizmos and picking

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

The selected object's translate/rotate/scale gizmo is drawn through the VulkanEngine
scene overlay hook (`RenderingManager::setSceneOverlay`, one callback per frame into the scene window's own
`ImDrawList`) rather than a separate render pass; `RenderSystem::viewParams` gives the overlay the view
matrix and projection whichever camera (free-fly or a component `Camera`) is currently live. `ViewportGizmo`
builds a `gizmo::Input` from the target's `Transform` each frame, runs the headless `gizmo::` core (hit-
testing and drag math, `source/libs/editor/Gizmo.{h,cpp}`; a world-space drag is mapped back to local through the
parent's world frame, which `Input` carries), and draws the result through `GizmoRenderer` -
an interface so a native backend could replace `ImGuiGizmoRenderer` later without `ViewportGizmo` changing.
A drag sends a per-frame `editComponent` while it moves (matching the Inspector's own slider drags, so
other connected views see it move smoothly) and one committed edit - one undo entry - on release; a click on a
handle without movement sends nothing. That bookkeeping is `GizmoEditSending` (headless, in `ECS3DTests`):
a frame sends only when it changed the Transform's live local pose, and the commit fires only
when the final `serialize()` differs from the drag's start.

A new `gizmo::State` is in local space and snapping defaults off (a stored preference wins). While the editor views
the scene through a scene camera (`shouldDrawTransformGizmo`, `GizmoVisibility.h`), `EditorApp` hands
`ViewportGizmo::update` no target, so it draws nothing, `capturesMouseAt` reports false and an in-flight drag ends
through the same abandon path as a vanished target (commit and gesture-ended callbacks fire, nothing resumes).

`EditorApp::handlePicking` runs from `run()` after `sendInput()`, not from the overlay callback: a mouse that
moves onto a handle and presses in the same frame is handled by `ViewportGizmo::capturesMouseAt(mouse)`, which
re-runs the headless core on copies of the last frame it actually drew (no side effects) with the given mouse
position and the button up, so `handlePicking` can ask "is this mouse over a handle" without waiting for this
frame's own overlay to run. The renderer is in `PickMode::onRequest` (set once by the `GpuAssetCache` constructor,
which both apps build), so it picks only for a `RenderSystem::requestPick` and the cursor costs nothing on the
frames in between. `handlePicking` requests a pick only for a click `shouldRequestPick` accepts (`PickClick.h`,
headless): the left button's rising edge, Ctrl held, the cursor over the scene (`canMousePick`) and no gizmo
handle capturing it, so grabbing a handle leaves the selection as it is. The request's callback runs inside a
later `render()` with the owner's uuid, and `applyPickResult` applies it with the Shift state read at click time:
Ctrl+click selects the picked object (replacing the selection) or clears it on empty space;
Ctrl+Shift+click toggles the picked object in/out of the selection, and does nothing on empty space; a plain click
(focusing the viewport, dragging a gizmo handle) leaves the selection as it is. An object that left the scene
before the callback ran changes nothing. `RenderSystem` maps the picked render object or terrain surface back to
its object uuid from a table it fills at the submissions only while a request is in flight, and clears when the
last one resolves. The editor sets `ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true` (in `setupImGuiStyle`)
so a drag inside a detached scene window's body drives the gizmo instead of moving the OS window.
While terrain mode is on (see [editor.md](editor.md)), the editor owns the left button over the scene: `EditorApp`
passes `shouldDrawTransformGizmo` the mode's state so the gizmo draws nothing, and sets `PickClick::toolOwnsMouse`
so `shouldRequestPick` refuses every click. The right-drag camera fly is untouched. `EditorApp::updateTerrainMode`
runs from the scene overlay callback (inside a `Terrain mode` profiler scope), because the overlay already has the
view and the scene rect and runs before the 3D pass records its lines. It builds the pointer ray with
`gizmo::mouseRay`, moves it into the terrain's local space (`TerrainFrame` in `TerrainCursor.h`: position plus
orientation, no scale, as in `TerrainStreamFocus`), and calls `terrain::raycast` with the far plane as the longest
distance. A hit draws the brush ring (`brushRing`: 48 points of a circle of `effectiveBrushRadius` in the plane
perpendicular to the hit normal, +Y for a degenerate normal) with `Renderer3D::renderLine`, in the tool's color; no
hit draws nothing and begins nothing. Looking through a scene camera hides the cursor and blocks strokes while the
mode stays on (`makeTerrainPointer`), and a running scene leaves the mode altogether (`TerrainModeContext`).

The frame's decisions are `TerrainBrushSession::advance`, which `nextStrokeStep` drives. A stroke begins on a fresh
left press over the scene that has a hit, so a drag that starts on a panel never begins one, and reads the brush
template (`terrainBrushTemplate`, from the tool and its preferences) once, at that press. While the button is held
each frame's `moveTo` performs the applications `TerrainStroke` spaces a quarter radius apart: `applyBrush`,
`recordApplication`, `terrain::commit` on the store's own volume and a `terrainEdit` brush message, which is the
prediction. A frame with no usable hit (off the terrain, over a panel, or the camera flying) paints nothing and calls
`TerrainStroke::breakPath`, so the next hit starts a new segment instead of bridging the gap; the stroke stays alive
and a release still ends it. After each application the Terrain components' bounds are invalidated and the
renderer's surface follows the volume revision.

The store's revisions are never touched by a prediction, and the echo does not simply replace it. The server flushes
its echo every 50 ms while the view is ahead of it, so while a stroke is active `EditorApp::handleTerrainChunks`
passes `TerrainBrushSession::holdFor(terrain)` (the stroke's touched chunks, for that terrain only) to
`TerrainViewSync::apply`, which skips those entries, full or hash-only, and requests nothing for them. Releasing the
button, leaving the mode, losing the connection or the volume ends the stroke through `endFor`: one redistance per
`redistanceBoxes` piece of the stroke's box that holds a touched chunk (so a long diagonal stroke costs what it
touched, not its bounding box) is applied to the volume locally, in order, each piece's edits committed before the
next is computed, and sent, so the prediction matches what the server computes from the same messages. Then one
`terrainChunks` request names every touched chunk and every chunk the local redistance changed (split at
`maxTerrainChunksEntries`), and the server answers after the redistance, on the same ordered connection, with its
final chunks and their revisions. Those chunks stay held until their full answer arrives: `handleTerrainChunks` calls
`TerrainBrushSession::confirmChunks` with each message before applying it, because an echo the server flushed between
the stroke's last brush and its redistance would otherwise read as a mismatch. Without editing the stroke
is dropped and nothing is sent. When the volume was replaced under the stroke the server still applied its brushes,
so the redistance pieces are sent, but the replaced volume is left alone and no final state is requested.
Each light also gets a sprite at its position, drawn through `RenderSystem::drawLightGizmos`, which only the
editor calls, and only while the scene is stopped and ray tracing is off (`LightGizmoVisibility.h`): the ray
traced path traces every submitted object as lit geometry and has no raster pass to draw an icon in. A pick of the
sprite resolves to the light's uuid like the light object's model does, so Ctrl+click selects the light. The icon carries alpha, but the unlit textured pipeline ignores it for now,
so it reads as a slate badge until that pipeline gains an alpha cutoff. The shared
quad model and icon load once through `GpuAssetCache`; a failed load is logged once and leaves lights
without sprites.
A scene asset tile also drags onto the viewport to make that scene active: the same scene overlay callback
opens an ImGui drop target over the scene image (`EditorApp::acceptSceneDrop`), accepting only
`assetDragDrop::scene` payloads that `sceneFromPayload` resolves to a registered Scene record, and loads
through `onLoadScene` exactly like a double-click, with the same editable-server gate and no unsaved-changes
prompt (switching scenes discards no authored edits; runtime changes of a playing scene are still discarded
when it stops). Dropping the already-active scene does nothing, since reloading it would restart it.
