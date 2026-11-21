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
`userDataDirectory`/`defaultLogFile` resolve the per-user, per-machine directory (settings and logs live there),
and an overload takes the environment lookup so tests need not touch the real environment; `LogSetup`'s
`addFileSinkFromArguments` registers an app's `FileSink` from its command line. Depends on nothing but the standard
library and, on Windows, the console API (for `openConsoleWindow`). Apps register a `ConsoleSink` and a `FileSink`
at startup.

## ECS3DSettings

`ECS3DSettings` — `SettingsStore`, per-user editor and client preferences on disk (in the directory `ECS3DLog`'s
`userDataDirectory` resolves), plus `Keybinds` (`KeyChord`/`parseChord`/`formatChord`, the `EditorAction`
catalogue, and the bijective `KeybindTable`) - the headless keybind model, the numeric key/mod values of GLFW
(the windowing library) spelled out as literals so this library still depends on nothing but json and ECS3DLog -
`EditorCameraSettings` (clamped get/set for the editor viewport's free-fly camera speed, applied to
`vke::Camera::setSpeed`), `EditorViewportSettings` (the viewport grid's show/hide preference, default shown),
`ClientDisplaySettings` (the client's fullscreen preference, kept in its own `client-settings.json`),
`FramePacingSettings` (vsync and the frame rate cap under separate client and editor keys, the cap clamped on read
and write, `0` unlimited; the `resolve*` helpers let a command-line flag override for one launch without
persisting; the client keys live in `client-settings.json`, the editor keys in `settings.json`),
`EditorLayoutSettings` (the ImGui ini text under `editor.layout.imgui`, plus `hasDockLayout`, which decides whether
a saved string holds a dock tree),
`EditorGizmoSettings` (clamped get/set for the viewport gizmo's snap toggle and translate/rotate/scale step
sizes; returns plain values rather than a `gizmo::Snap`, which keeps settings below the editor lib in the
dependency direction under Build System), and `RecentProjects` (the recent-projects list the editor and the
launcher share; headless, schema in `source/libs/settings/RecentProjects.md`). **Not** project data: see
Development Principles.

## ECS3DData

`ECS3DData` — the foundation. Component **data** (Transform, RigidBody, ModelRenderer, LightRenderer, Colliders
(box, sphere and capsule; their cached bounds, support geometry and box frame key on `Transform::getWorldUpdateID`,
which also moves when an ancestor does or the object is reparented, plus `Collider::getGeometryRevision` for the
collider's own fields; a convex mesh collider also holds sim-filled caches: face outlines, world face normals and faces, and its local mass distribution, which the sim carries to the world through the collider's world matrix), Script, PlayerController, Camera) - whose float and
vec3 setters ignore non-finite input, since a non-finite float saves as json null and makes the file unreadable -
`Transform`'s world getters compose with the parent as `parentWorld * local` (position through the parent's
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
larger of the object's x and z scale, and its half-height with y. `terrain/` holds the sparse signed-distance volume in
`namespace terrain`: `TerrainVolume` maps chunk coordinates to immutable 32-sample `TerrainChunk`s that an edit swaps
copy-on-write (its `revision()` is unique across all volumes and changes with every edit), `encodeChunk`/`decodeChunk` are the chunk byte format, and `Sampler` reads trilinear distance and
gradient from it, which `raycast` sphere-traces with absent chunks skipped.
`TerrainStreamPlan` is the headless planner for streaming a volume to a joining view: it queues every chunk nearest a
focus first, `next` takes a batch whose wire-framed size fits a byte budget (the first chunk always goes, so the stream
cannot stall), and `requeue` sends a coordinate again after an edit, reading the chunk from the volume at `next` time.
A terrain is an `AssetType::Terrain` record whose path is always `terrainAssetPath(uuid)` (`terrain/<uuid>`;
`registerAsset` refuses any other, since the path names a folder under the project root). The record never carries
data: `TerrainStore` holds each terrain's volume and `TerrainDescription` (voxel size, material palette), and
`reconcile` follows the registry while keeping the volume of every terrain that stays.
Each app keeps a `TerrainBinder` that points every scene's `Terrain` components at their store volumes once a tick.
It caches the components a scan found and scans again only when the scene set or `ObjectManager::structureRevision`
changes. That process-wide counter advances whenever any manager's object list changes or any object gains or loses
a collider, so a new path that changes either must go through `ObjectManager` or `Object::add/removeComponent`.
On disk a terrain is `<root>/terrain/<uuid>/`: a `terrain.json` index (voxel size, palette, and per region a tag, the file's byte size, a hash of its bytes and every chunk's hash) and one `r.<x>.<y>.<z>.<tag>.bin` file per 8x8x8 chunk region, where the tag hashes the region's chunk set. A save never overwrites a file the current index names: it writes the missing region files, replaces the index through a temporary file, and only then deletes region files the new index does not reference, so a process crash or failed write at any step leaves the old index with all its files intact. That holds for one writer per terrain folder (saves are not synchronized), and a power loss can still lose the latest save because files are not flushed. A save rewrites a region file whose size or byte hash disagrees with the index. Load follows the index and checks every region file's hash and every decoded chunk's hash.
`TerrainOps` turns an operation into `ChunkEdits` (replacement chunks, null for air) through the shared `editSamples`, which caps an edit at `maxEditChunks`, and `commit` applies them; `fillFlat`/`fillFromHeightmap` generate ground that ends at a chunk boundary. `redistance` fast-sweeps a box back to a distance-like slope while holding every sample next to the surface fixed, so the zero crossing does not move. Brushes (`BrushOp`, add/subtract/smooth/flatten/paint) name their `dirtyBox` before `applyBrush` runs and refuse a radius over `maxBrushRadiusVoxels`, and `pack`/`unpackBrushOp` are their wire form.
**No Vulkan, no ImGui.**

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
remembers it, since the engine's loaders throw rather than return null; `getModel` and `getTexture` then
return null, which every caller tolerates. `getRenderObject` alone fills a slot it cannot resolve
with a placeholder from the headless `MissingAssetFallback.h`: the magenta and black `missing_texture.png`, and for a
missing model the `missing_model.glb` cube wearing that texture. Both are built-in files loaded once and kept across
project resets, and are never registered in a project. A missing asset is logged once per uuid, and an entry showing
a placeholder is rebuilt when the real asset resolves, so an undone delete draws on the next frame), `InputCapture`,
`ThumbnailImage` (headless letterbox resize and PNG encode for project thumbnails, via stb_image_write; nothing calls
it yet). `RenderSystem` gathers each object's render components in one pass over its component map through the
headless `RenderComponents.h`, and prunes its per-uuid caches only when the set of drawn uuids changes, which the
headless `LiveObjectTracker.h` detects with an order-independent signature. A model's render object gets its
position, scale and rotation pushed only when `Transform::getWorldUpdateID` differs from the last push, which the
headless `TransformPushGate.h` remembers per cached render object, so a rebuilt render object is always pushed.

A `Terrain` component is drawn through the engine's volume surface. `GpuAssetCache::getTerrainSurface` keeps one
surface per terrain uuid, and the surface reads a `TerrainSurfaceFeed` (headless), the chunk snapshot a
`TerrainVolumeSource` answers fills from on the engine's worker threads. The feed swaps the snapshot and reports the
change to the surfaces under one exclusive lock, so no fill sees an edit before its `invalidate`; no worker holds the
volume itself. Bounds are the occupied chunks plus an air sample, rounded to `surfaceBoundsStep`. A terrain that
outgrows them gets a larger surface built behind the drawn one, promoted as soon as its `pendingFraction()` is no
higher than the drawn surface's (so a terrain whose chunks change every frame still shows its growth), and one past
`maxSurfaceChunks` is logged once and not drawn or grown. Changed chunks are reported once per frame per terrain.
Surfaces are released through the engine's deferred release on a new voxel size, a project reset, or when no object
draws the terrain. The decisions are the headless `TerrainSurfacePlan.h`, and the model matrix has no scale because
terrain space is metric. A terrain is pickable through `isSelected(object uuid)` and outlined when its object is
selected; the engine matches a pick to the surface it was drawn with, so a click on the frame a larger surface is
promoted selects nothing. Submissions wait until every object has voted, and the surface casts shadows when any
object drawing it has `castShadows` on (the headless `TerrainShadowVotes.h`). Depends on `ECS3DData` +
`VulkanEngine`.
