# Library reference

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

## ECS3DLog

`ECS3DLog` — a central log sink: `LogLevel`/`LogCategory` (+ `toString`), `LogEntry`, the `LogSink` interface,
`ConsoleSink` (stdout/stderr, today's behavior), `RingBufferSink` (recent entries for the editor's console
panel), `RemoteLogSink` (a bounded queue a server drains to forward its log to editor connections over the wire
— see [logging.md](logging.md)), `FileSink` (UTC-timestamped lines to a file, truncated per run), `LogFilter`
(level/category toggles + a case-insensitive text search over a `LogEntry`, headless so it is testable without
ImGui) and `formatEntry` (the shared `[level][category] message` rendering, prefixed with the time of day, used
by the panel and its copy button), and the process-wide `Log` facade; `ConsoleWindow`'s `openConsoleWindow`
allocates and attaches a console for GUI (graphical user interface) subsystem apps; `UserDataDirectory`'s
`userDataDirectory`/`defaultLogFile` resolve the per-user, per-machine directory (settings and logs live there);
`LogSetup`'s `addFileSinkFromArguments` registers an app's `FileSink` from its command line. Depends on nothing
but the standard library and, on Windows, the console API (for `openConsoleWindow`). Apps register a
`ConsoleSink` and a `FileSink` at startup.

## ECS3DSettings

`ECS3DSettings` — `SettingsStore`, per-user editor preferences on disk (in the directory `ECS3DLog`'s
`userDataDirectory` resolves), plus `Keybinds` (`KeyChord`/`parseChord`/`formatChord`, the `EditorAction`
catalogue, and the bijective `KeybindTable`) - the headless keybind model, the numeric key/mod values of GLFW
(the windowing library) spelled out as literals so this library still depends on nothing but json and ECS3DLog -
`EditorCameraSettings` (clamped get/set for the editor viewport's free-fly camera speed, applied to
`vke::Camera::setSpeed`), `EditorViewportSettings` (the viewport grid's show/hide preference, default shown),
`EditorGizmoSettings` (clamped get/set for the viewport gizmo's snap toggle and translate/rotate/scale step
sizes; returns plain values rather than a `gizmo::Snap`, which keeps settings below the editor lib in the
dependency direction under Build System), and `RecentProjects` (the recent-projects list the editor and the
launcher share; headless, schema in `source/libs/settings/RecentProjects.md`). **Not** project data: see
Development Principles.

## ECS3DData

`ECS3DData` — the foundation. Component **data** (Transform, RigidBody, ModelRenderer, LightRenderer, Colliders
(box, sphere and capsule; their cached bounds and box mesh key on `Transform::getWorldUpdateID`, which also
moves when an ancestor does or the object is reparented), Script, PlayerController, Camera) - whose float and
vec3 setters ignore non-finite input, since a non-finite float saves as json null and makes the file unreadable
- `Transform`'s world getters compose with the parent as `parentWorld * local` (position through the parent's
turn and scale, orientation by quaternion with the `RotationConvention.h` Euler mapping every system shares,
scale component-wise; a child box collider's rotation composes through the parent while its position offset
stays world-axis) - `Object`/`ObjectManager`, scenes (a `SceneAsset` also carries an ordered list of scene
scripts - `Script` data entries, at most one per class - that saves, packs and is restored on stop with the
object tree), `collisions/ConvexHull.h`/`QuickHull.h` (the headless 3D Quickhull behind `buildConvexHull`, which
rebuilds a hull over `maxConvexHullVertices` from the vertices furthest along that many Fibonacci-sphere
directions, and refuses any input `HullVerification.h` cannot verify as a closed convex hull; the face type and
plane math are `HullFace.h`, the starting tetrahedron search `InitialSimplex.h`, all in `namespace hull`),
`ProjectOrigin` (the headless template rules: `isTemplateProject`, `untitledTitle`, `defaultSaveFolder`,
`saveActsAsSaveAs`), `AssetRegistry` (incl. prefab bodies), `ComponentRegistry`, `ProjectFingerprint`
(`fingerprintBytes`/`fingerprintProjectFile`, the 64-bit Fowler-Noll-Vo 1a content hash a snapshot carries to
name the server's project), `ProjectPaths` (headless `locateProject` - a `--project` folder or `.ecs3d` file to
its project file and root - and `resolveAssetPath`), `ProjectSerializer` (JSON file save/load - a load that
fails names the scene and the object it choked on) / `ProjectPacker` (binary wire snapshot), `Replication`,
`PlayerSlots` (`PlayerSlotTable`, the connection-to-slot bookkeeping, plus
`playerSlotsInScene`/`findPlayerCamera`), `edits/` (`EditCommand`/`EditHistory` — the undo/redo stack —
`HistoryScope` (which scene the history was recorded against), `PlaySessionHistory` (the authored/play-session
history swap), and `RecordEdits`, which derives the command for an edit the editor is about to send from its
pre-edit replicated view; see [undo-redo.md](undo-redo.md)), and `scenes/SceneScriptEdits` (the scene script
list's `sceneEdit` op builders and apply). `templates/TemplateManifest`
(`parseTemplateManifest`/`loadTemplateManifest`, the headless `template.json` parser). A `CapsuleCollider`
points along local +Y, and its `height` is the total height including both caps. Its radius scales with the
larger of the object's x and z scale, and its half-height with y. **No Vulkan, no ImGui.**

## ECS3DRender

`ECS3DRender` — `RenderSystem` (draws models/lights, pick feedback, outlines of selected objects and collider
gizmos via `Renderer3D::renderOutline`, colored by the headless `OutlineColors.h`, the editor-only light gizmo -
a camera-facing, constant-screen-size billboard per `LightRenderer` drawn through `drawLightGizmos` and pickable
like a model, whose orientation/size math is the headless `Billboard.h` (with `LightGizmoVisibility.h` deciding
when it is drawn) - drives the `vke::Camera`/`Renderer3D` view from the scene's active `Camera` component, and
reports that same view back through `viewParams` - the view matrix + fov/near/far the editor's viewport
transform gizmo builds its `gizmo::View` from), `GridVisibility.h` (headless `shouldShowGrid`: the editor grid
shows only when the preference is on and the View is free-fly; `RenderSystem::setGridVisible` applies it each
frame, and the client hides the grid), `GpuAssetCache` (UUID → `vke` GPU objects, incl. the per-owner light
gizmo and the collider gizmo. The collider gizmo takes a part index, so a shape drawn from several meshes keeps
one render object per part, and `trimColliderGizmo` releases parts a shape no longer needs. A capsule is a
cylinder (`models/cylinder.glb`) plus two end spheres, placed by the headless `CapsuleGizmo.h`. A convex mesh
collider's hull gets its own per-owner model that `createModel` builds from the hull's triangles through the
headless `HullGizmoMesh.h`, flat-shaded and outlined like the box gizmo, rebuilt and the old model released only
when the hull's content changes, and released when the object stops drawing a hull gizmo. All parts share one
outline color so the outline mask merges them; the collider gizmo's shared mesh and texture, and the model and
texture assets that `getModel`/`getTexture` resolve by uuid, and only a record of that type (keyed by asset
uuid, logged by path), load once through the headless `SharedAssetLoads.h`, which logs a throwing load once and
remembers it, since the engine's loaders throw rather than return null; `getModel`, `getTexture` and
`getRenderObject` then return null, which every caller tolerates, so a missing or corrupt asset file shows
nothing instead of ending the app), `InputCapture`, `ThumbnailImage` (headless letterbox resize and PNG encode
for project thumbnails, via stb_image_write; nothing calls it yet). Depends on `ECS3DData` + `VulkanEngine`.
