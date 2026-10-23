# AGENTS.md

> **Living document.** Keep this current. When architecture, conventions, workflows, dependencies, or
> repository structure change, update this file in the same change. Keep it concise (target 2–5 pages) —
> summarize systems and link to code; don't duplicate what file names, CMake, or source already make obvious.
> Read this before making changes.

## Project Overview

- **ECS3D** is a 3D game engine built around a data-oriented Entity Component System, rendered by the
  **VulkanEngine** library (a sibling project, [SharkFinPro/VulkanRenderer](https://github.com/SharkFinPro/VulkanRenderer),
  fetched via CMake `FetchContent`, tag `main`).
- The engine is **authoritative client/server multiplayer** by design. A headless server owns the
  simulation; clients render a replicated view and send input. Singleplayer is the same server spawned
  as a local child process. This is not an add-on — it is the shape of the whole codebase.
- The C++ side owns the engine; **C# is hosted in-process via CoreCLR** (nethost/hostfxr) for two
  purposes: gameplay scripting (`ScriptBridge`) and the network transport (`ECS3DNetTransport`). C++
  owns the data and protocol; C# owns the sockets and user script execution.
- Four executables ship (see Applications): **ECS3DServer**, **ECS3DClient**, **ECS3DEditor** (all C++),
  and **ECS3DLauncher** (a standalone C# Avalonia app — see [its own AGENTS.md](source/apps/launcher/AGENTS.md)).
- Scope today: a working editor + play/edit servers with physics (Gilbert-Johnson-Keerthi (GJK) and Expanding Polytope Algorithm (EPA) collision; rigid bodies),
  C# scripting, asset import, scene management, and full snapshot/delta replication over TCP (Transmission Control Protocol).

## Role and Tools

- **Role:** you are a coding agent working for the ECS3D developer on this repository, extending the
  engine described above while following the conventions below.
- **Tools:** the project builds with CMake presets (`CMakePresets.json`) and tests with GoogleTest through
  CTest. The developer runs builds and tests, not the agent; see AI Agent Guidelines.

## Repository Structure

| Path | Responsibility |
|------|----------------|
| `.clang-format`, `.editorconfig` | The tree's style, written down: 2-space indent, 120 columns, braces on their own line for functions and control flow. **Nothing has been reformatted to match them yet** - that is a separate one-off commit, so read them as intent, not as a description of every file. |
| `CMakeLists.txt` (root) | Top-level config: C++23, `bin/` output when top-level, `compile_commands.json`, `include(CTest)`, the `ECS3D_SANITIZE`, `ECS3D_COVERAGE` and `ECS3D_PROFILE` options, `CMAKE_CXX_SCAN_FOR_MODULES` off by default, MSVC (the Microsoft Visual C++ compiler) export-all-symbols. Then `add_subdirectory(source)`. |
| `CMakePresets.json` | Configure presets only: `ecs3d-debug`, `ecs3d-release`, `ecs3d-sanitize`, `ecs3d-coverage`, `ecs3d-profile`, writing to `cmake-build-ecs3d-debug` / `-release` / `-sanitize` / `-coverage` / `-profile`. `cmake --preset ecs3d-debug` then `cmake --build cmake-build-ecs3d-debug --target check` is the documented way in. |
| `source/libs/` | All reusable engine libraries. `libs/CMakeLists.txt` fetches shared deps (json, glm, uuid, nfd, VulkanEngine) and the managed-assembly helpers, then adds each lib. |
| `source/libs/log/` | `ECS3DLog` — a central log sink: `LogLevel`/`LogCategory` (+ `toString`), `LogEntry`, the `LogSink` interface, `ConsoleSink` (stdout/stderr, today's behavior), `RingBufferSink` (recent entries for the editor's console panel), `RemoteLogSink` (a bounded queue a server drains to forward its log to editor connections over the wire — see Logging below), `FileSink` (UTC-timestamped lines to a file, truncated per run), `LogFilter` (level/category toggles + a case-insensitive text search over a `LogEntry`, headless so it is testable without ImGui) and `formatEntry` (the shared `[level][category] message` rendering, prefixed with the time of day, used by the panel and its copy button), and the process-wide `Log` facade; `ConsoleWindow`'s `openConsoleWindow` allocates and attaches a console for GUI (graphical user interface) subsystem apps; `UserDataDirectory`'s `userDataDirectory`/`defaultLogFile` resolve the per-user, per-machine directory (settings and logs live there); `LogSetup`'s `addFileSinkFromArguments` registers an app's `FileSink` from its command line. Depends on nothing but the standard library and, on Windows, the console API (for `openConsoleWindow`). Apps register a `ConsoleSink` and a `FileSink` at startup. |
| `source/libs/protocol/` | `ECS3DNetProtocol` (INTERFACE lib): `Protocol.h` — the wire format (`MessageType`, `Message`/`MessageReader` binary framing, `Role`, ports). Depended on by everything that touches the wire. |
| `source/libs/settings/` | `ECS3DSettings` — `SettingsStore`, per-user editor preferences on disk (in the directory `ECS3DLog`'s `userDataDirectory` resolves), plus `Keybinds` (`KeyChord`/`parseChord`/`formatChord`, the `EditorAction` catalogue, and the bijective `KeybindTable`) - the headless keybind model, the numeric key/mod values of GLFW (the windowing library) spelled out as literals so this library still depends on nothing but json and ECS3DLog - `EditorCameraSettings` (clamped get/set for the editor viewport's free-fly camera speed, applied to `vke::Camera::setSpeed`), `EditorViewportSettings` (the viewport grid's show/hide preference, default shown), `EditorGizmoSettings` (clamped get/set for the viewport gizmo's snap toggle and translate/rotate/scale step sizes; returns plain values rather than a `gizmo::Snap`, which keeps settings below the editor lib in the dependency direction under Build System), and `RecentProjects` (the recent-projects list the editor and the launcher share; headless, schema in `source/libs/settings/RecentProjects.md`). **Not** project data: see Development Principles. |
| `source/libs/data/` | `ECS3DData` — the foundation. Component **data** (Transform, RigidBody, ModelRenderer, LightRenderer, Colliders (box, sphere and capsule; their cached bounds and box mesh key on `Transform::getWorldUpdateID`, which also moves when an ancestor does or the object is reparented), Script, PlayerController, Camera) - whose float and vec3 setters ignore non-finite input, since a non-finite float saves as json null and makes the file unreadable - `Transform`'s world getters compose with the parent as `parentWorld * local` (position through the parent's turn and scale, orientation by quaternion with the `RotationConvention.h` Euler mapping every system shares, scale component-wise; a child box collider's rotation composes through the parent while its position offset stays world-axis) - `Object`/`ObjectManager`, scenes (a `SceneAsset` also carries an ordered list of scene scripts - `Script` data entries, at most one per class - that saves, packs and is restored on stop with the object tree), `collisions/ConvexHull.h`/`QuickHull.h` (the headless 3D Quickhull behind `buildConvexHull`, which rebuilds a hull over `maxConvexHullVertices` from the vertices furthest along that many Fibonacci-sphere directions, and refuses any input `HullVerification.h` cannot verify as a closed convex hull; the face type and plane math are `HullFace.h`, the starting tetrahedron search `InitialSimplex.h`, all in `namespace hull`), `ProjectOrigin` (the headless template rules: `isTemplateProject`, `untitledTitle`, `defaultSaveFolder`, `saveActsAsSaveAs`), `AssetRegistry` (incl. prefab bodies), `ComponentRegistry`, `ProjectFingerprint` (`fingerprintBytes`/`fingerprintProjectFile`, the 64-bit Fowler-Noll-Vo 1a content hash a snapshot carries to name the server's project), `ProjectPaths` (headless `locateProject` - a `--project` folder or `.ecs3d` file to its project file and root - and `resolveAssetPath`), `ProjectSerializer` (JSON file save/load - a load that fails names the scene and the object it choked on) / `ProjectPacker` (binary wire snapshot), `Replication`, `PlayerSlots` (`PlayerSlotTable`, the connection-to-slot bookkeeping, plus `playerSlotsInScene`/`findPlayerCamera`), `edits/` (`EditCommand`/`EditHistory` — the undo/redo stack — `HistoryScope` (which scene the history was recorded against), `PlaySessionHistory` (the authored/play-session history swap), and `RecordEdits`, which derives the command for an edit the editor is about to send from its pre-edit replicated view; see Editor Undo/Redo below), and `scenes/SceneScriptEdits` (the scene script list's `sceneEdit` op builders and apply). `templates/TemplateManifest` (`parseTemplateManifest`/`loadTemplateManifest`, the headless `template.json` parser). A `CapsuleCollider` points along local +Y, and its `height` is the total height including both caps. Its radius scales with the larger of the object's x and z scale, and its half-height with y. **No Vulkan, no ImGui.** |
| `source/libs/sim/` | `ECS3DSim` — `PhysicsSystem` (integration, forces, response; linear velocity is displacement per tick while angular velocity is a world-space axis in degrees per second, so convert angular velocity to radians per tick before combining the two - `applyVelocityChange`, `applyImpulse`, `handleCollision` and `CollisionSystem::fixedUpdate` take `dt` for that; the inertia tensor is per unit mass and comes from the body's collider shapes in world geometry (`InertiaTensor.h`: a solid sphere, box, capsule - a cylinder and two hemispheres - or a hull summed from tetrahedra to its triangles; a compound body weights each collider by its volume about the owner's position, a trigger adds nothing, and a body with no collider is a solid box whose half-extents are its scale), so a velocity change moves and turns a body alike whatever its mass while an impulse is divided by the mass for both; it turns the orientation by composing a rotation onto it rather than by adding to the Euler angles, and writes the result back through `Transform::setWorldRotation` since the rotation it turned is combined with the parent's, and an off-center impulse reaches it through the inertia tensor taken in world axes - a box manifold is resolved with one impulse at `supportPoint`, the center of mass clamped into the manifold, sized to stop that point's own velocity, spin included, through its effective mass, and leaving a body already moving away from its support as it is - a dynamic pair sizes the impulse against both bodies' inverse mass and inertia and trades the closing speed elastically (below `minBounceSpeed`, two units a second, it does not bounce), and splits the separation by inverse mass too, but a body that rested on a support last tick hands the part of any push from another body that points into that support to it, so a stack holds whatever its masses and what lands on it stops as on static geometry, without a bounce - and its support's friction holds the part along the support too, inside the friction cone and up to what that friction can take (`PhysicsSystem::squeeze`), so a light body is squeezed out from under a heavy one only by the excess, pushed through its center; "into" and "along" are taken against the support's contact normal (`RigidBody::getSupportNormal`, the flattest upward contact normal of the previous tick, world up when none), so a stack on a slope holds as on flat ground, and the load a body passes to its support is taken along that normal too, and a vertical speed under `minRiseSpeed` does not count as a body rising; a contact between two resting bodies is resolved only from the upper one, since resolving it again from below leaves the pair touching exactly, which the sweep misses, but one the resting body below does resolve, because the body landing on it had no contact in its own pass, has its friction and passed-on load resolved with the roles swapped, so it acts as it would from above, the push landing at the landing body's own center of mass clamped into the manifold; then Coulomb friction opposes the contact point's sliding up to the coefficient (the geometric mean of two bodies', the body's own against static geometry) times the impulse pressing the pair together, which includes the load stacked on the body (`RigidBody::getStackedLoad`, reset each tick), acting through the center on a face under the center of mass and at the point otherwise, and a resting body's support takes a drag from above first, up to what its own friction holds, leaving the body unturned and recording the impulse along the support it held (`RigidBody::getHeldImpulse`, reset each tick, a vector so pushes the opposite way cancel) so the body's own contact with that support has only the rest; a face contact also resists turning about its normal, up to friction times load times two thirds of the manifold's mean radius, and a sphere's contact resists rolling at a tenth of that friction, since neither slides at the support point (rolling resistance and the resting-face and flush logic both use the collider `CollisionSystem` hands `handleCollision`, not the body owner's, since on a compound body it belongs to a child; both take the faces of that collider and of the support from `collisions::supportFacesOf`, so a box and a convex mesh rest alike; a face with over four corners is reduced to its convex outline (`collisions/PlanarHull.h`) before the center-of-mass test; a sphere is not laid flush; a capsule lying in a line contact under its center of mass is turned into its support's face plane; a body whose resting face meets a neighbor within 30 degrees (a finely faceted hull, like a drum) rolls on rather than landing on a facet, where a box lands flat; and `layFlush` turns the owner's world orientation by the angle between the two faces); a body that friction fully holds on a slope is moved back by the slide its gravity and queued forces caused (`holdAgainstDrive`, not for spheres or capsules, which roll), so it stays put below the friction angle; nothing else damps linear velocity, so a body in flight keeps its horizontal speed; before the impulse, each contact point sheds, inertia-weighted, only the spin driving it into its support, repeated over the points until none sheds more, except that a body which rested on that plane last tick (its recorded support normal agrees), with its center of mass over the face, keeps spin that turns it past the flush window in one tick, so a push can tip it over its far edge instead of sliding it upright; a landing or finely faceted body still sheds it, and a contact whose normal is within a degree of level counts as a side contact, whichever way rounding tilts it, and a body resting on something below it has leftover spin under `restAngularSpeed` dropped, so it comes exactly to rest; a body whose center of mass is over a face it rests on, within the degree or so the manifold already counts as flush, is turned flush with the support's face, since nothing torques it the rest of the way down; a box on an edge or corner whose spin would carry its face flat against a box support within the tick, with its center of mass over that face, lands on the face that tick instead, since one tick of edge support turns a box a degree or more and it would otherwise rock from edge to edge). A sphere or capsule rolls with its own world radius as the rolling arm, whatever its offset from the body's owner. Lying along a line contact it rolls only about its own axis, and a body resting on a capsule is slowed by that rolling resistance too. A capsule lying on a flat support touches along a line, so its center of mass between the two contact points counts as supported. Friction there rolls it only by the part across the line; the part along acts through the center, since the line's far end holds the capsule against pitching. It grinds against turning about the normal with the mean lever arm of a load spread evenly along the line. Take a and b as the distances from the center of mass to the two ends, and that arm is (a^2 + b^2) / (2 (a + b)). `CollisionSystem` (sweep-and-prune in minX order - each edge scans forward only, testing a pair once from its lower-indexed edge when either side is dynamic and handing the hit to every dynamic side, with each edge's hits kept in ascending edge order - but responses resolve top-down by bounding-box minY so a stack settles the same whichever side of it the load sits), calling the narrow phase under `collisions/` — `NarrowPhase.h`'s `findContact`/`intersects` are its entry points. A pair of spheres and capsules skips GJK, since a sphere is a capsule with a zero-length core segment. That pair is solved from the closest points between the two core segments, treated as two spheres. Parallel overlapping capsules report two manifold points. Every other pair goes through GJK/EPA, where a capsule's contact feature is its cap points facing the other shape, clamped onto a box's footprint. EPA stops once the support point lies on the closest face's plane, and treats a face as visible only beyond a tolerance scaled by magnitude, since rounding would otherwise delete coplanar faces and leave a hole. The same tolerance decides whether a new face looks inward, since a vertex on its plane is noise. A rebuild that cannot close its horizon is undone and ends EPA. `collisions/SupportFaces.h` returns a box's or convex mesh's world-space planar faces (outward normal, counter-clockwise corners, center; coplanar hull triangles merged in the hull's own space) with `faceToward` and `clampToFace` queries; PhysicsSystem's resting logic uses it, and `supportFaceToward` builds the single face a box needs. A pair whose colliders resolve to the same rigid body, or where one object is an ancestor of the other, is skipped before the narrow phase. A box or hull face manifold also adds the reference face's corners that lie over the incident face, at the incident face's depth, and spreads the result to at most four points. A small face on a larger or turned one so keeps its whole footprint instead of two diagonal corners. A contact holds a resting body like a stacked load (`pressesIntoSupport`) only within 45 degrees of that body's support normal. A flatter push, such as a leaning domino, can tip it. `findSupport` decides whether the center of mass is over the support by dropping it straight down along world y, not along the contact normal. On a slope a body past its downhill edge then tips or rolls. Operates on `ECS3DData` via accessors. OpenMP if available. `queries/HullQueries` answers raycasts and sphere overlaps against a convex hull exactly, orienting each face away from the hull center so a mirrored transform still works. |
| `source/libs/render/` | `ECS3DRender` — `RenderSystem` (draws models/lights, pick feedback, outlines of selected objects and collider gizmos via `Renderer3D::renderOutline`, colored by the headless `OutlineColors.h`, the editor-only light gizmo - a camera-facing, constant-screen-size billboard per `LightRenderer` drawn through `drawLightGizmos` and pickable like a model, whose orientation/size math is the headless `Billboard.h` (with `LightGizmoVisibility.h` deciding when it is drawn) - drives the `vke::Camera`/`Renderer3D` view from the scene's active `Camera` component, and reports that same view back through `viewParams` - the view matrix + fov/near/far the editor's viewport transform gizmo builds its `gizmo::View` from), `GridVisibility.h` (headless `shouldShowGrid`: the editor grid shows only when the preference is on and the View is free-fly; `RenderSystem::setGridVisible` applies it each frame, and the client hides the grid), `GpuAssetCache` (UUID → `vke` GPU objects, incl. the per-owner light gizmo and the collider gizmo. The collider gizmo takes a part index, so a shape drawn from several meshes keeps one render object per part, and `trimColliderGizmo` releases parts a shape no longer needs. A capsule is a cylinder (`models/cylinder.glb`) plus two end spheres, placed by the headless `CapsuleGizmo.h`. A convex mesh collider's hull gets its own per-owner model that `createModel` builds from the hull's triangles through the headless `HullGizmoMesh.h`, flat-shaded and outlined like the box gizmo, rebuilt and the old model released only when the hull's content changes, and released when the object stops drawing a hull gizmo. All parts share one outline color so the outline mask merges them; the collider gizmo's shared mesh and texture, and the model and texture assets that `getModel`/`getTexture` resolve by uuid, and only a record of that type (keyed by asset uuid, logged by path), load once through the headless `SharedAssetLoads.h`, which logs a throwing load once and remembers it, since the engine's loaders throw rather than return null; `getModel`, `getTexture` and `getRenderObject` then return null, which every caller tolerates, so a missing or corrupt asset file shows nothing instead of ending the app), `InputCapture`, `ThumbnailImage` (headless letterbox resize and PNG encode for project thumbnails, via stb_image_write; nothing calls it yet). Depends on `ECS3DData` + `VulkanEngine`. |
| `source/libs/editor/` | `ECS3DEditorLib` — ImGui editing UI: `ComponentEditor` (per-type handlers, sharing `AssetSlot` for asset reference slots, which show `assetDisplay::name`, so a rename reads through, and only a record of the slot's own type (`assetDragDrop::slotRecord`), else None), `ObjectGUIManager` (object tree - Delete and Ctrl+D act on the whole current selection when it has 2+ objects, not just the row clicked: one atomic `batch` `sceneEdit`, one undo/redo entry - see Replication and Editor Undo/Redo below), `InspectorPanel` (the "Inspector" window — per-selection-kind dispatch) delegating the object kind to `ObjectInspector` (which, beside the per-frame value send, coalesces a whole continuous edit - a slider drag mutates the component in place every frame - into one before/after pair reported once no widget is active; a multi-object selection (`ObjectInspector::displayMulti`) shows the components shared across the selected objects - matched by `componentFieldDelta::componentSignature`, a collider's subtype or a script's class name counting as part of the "kind" - renders each shared component once against the first object, and propagates the top-level `serialize()` key(s) that changed to the other objects' own components via `ComponentFieldDelta.h`'s field-delta helpers, so an edit lands on the field that actually moved while each object's other fields keep whatever values they already had; the disagreeing keys are threaded into `ComponentEditor::GuiHandler` as a `MixedFields` set (empty on the single-selection path) so each differing field's own widget shows a distinct mixed state - a dash - instead of silently displaying the primary object's value, via ImGui's `ImGuiItemFlags_MixedValue` for the stock Drag/Checkbox/ColorEdit3 widgets this wraps (a Script field's bool still resolves a mixed click to true itself, since the flag only changes the rendering), and hand-drawn for `accentCheckbox`/`accentSlider`, which are not built on a stock widget; a residual "Mixed values" line still names the few fields with no per-widget indicator (an asset reference slot, the collider mask popup, a non-numeric script field), and each object's edit still goes out and is recorded through the normal single-object path, so undo records one entry per object; Add/Remove Component and the name field are left out of this view for now, since wiring them up would mean deciding which object they act on) and the asset kind to `AssetInspector` (per-`AssetType` views — read-only detail plus a display-name rename field and a delete button with a reference-count warning for the flat file assets; the **Prefab body is editable** — deserialized into a detached `TransientObject` and edited by a reused `ObjectInspector`, see Prefabs), `EditorSelection` (shared kind-tagged selection slot holding an ordered set of uuids rather than a single one, `Selection.h` - the back of the list is the primary, what the Inspector/gizmo show; in the tree, a plain click replaces it, Ctrl-click toggles membership, and Shift-click selects the contiguous range between an anchor uuid and the clicked row in the tree's current on-screen order, which `ObjectGUIManager` tracks itself each frame since collapsed nodes and the sort mode both affect what "on screen order" means; the range, row-action targets and sort rules live in the headless `ObjectTreeOrder.h`; the viewport has its own rule, described under Viewport gizmos below), `SettingsPanel` (the "Settings" window — a section nav beside the selected section's content, reading and writing `ECS3DSettings` directly since preferences are local, not replicated; Appearance edits the `EditorTheme.h` palette tokens live; Viewport drags the free-fly camera speed (`EditorCameraSettings`, applied to `vke::Camera::setSpeed` immediately) and the viewport gizmo's Snap toggle + translate/rotate/scale step sizes (`EditorGizmoSettings`); Keybinds lists every `EditorAction`, its current chord, and Rebind/Unbind/Reset controls, driving `KeybindDispatcher`'s capture mode and showing a conflict modal - naming the holding action, no reassign option - when a rebind targets an already-held chord), `KeybindDispatcher` (the one `vke::KeyCallbackEvent` listener that resolves a press against a `KeybindTable` and calls the registered handler, suppressed while ImGui wants the keyboard; also drives the Settings panel's rebind capture; actions registered with `KeyHold::repeatWhileHeld` (undo and redo) fire again while their chord stays held, after `keyHoldInitialDelay` and then every `keyHoldRepeatInterval`, driven by the headless `KeyHoldRepeat.h` and polled by `KeybindDispatcher::update()` once per frame; a handler with nothing left to do calls `stopRepeat()`), `SceneScriptsPanel` (the "Scene Scripts" window: add, remove, reorder and edit the current scene's scripts, docked on the Inspector's side; its pure decisions are the headless `SceneScriptsPanelLogic.h`, and the field widgets are `ScriptEditor.h`'s `drawScriptFieldWidgets`, shared with the object Script handler), `ConsolePanel` (the "Console" window — reads the editor's `RingBufferSink` through a `LogFilter`: level and category toggles with per-level counts, a text search, Copy (the filtered rows through `formatEntry` to the clipboard), Clear (remembers a sequence number rather than mutating the shared sink), and Auto-scroll; entries the connected server forwards land in this same sink, message-prefixed `"[server] "`, so they show and filter alongside the editor's own — see Logging below), `AssetBrowserPanel` (its type filter is a union of toggled chips, with an "All" chip to clear: `AssetFilter.h`), `AssetDisplay` (shared asset label/name/icon/color rules, header-only), `PlayControls.h` (the headless play/pause/stop enable rules for the Scene Status toolbar's fixed row of icon buttons, which stay drawn and gray out when unavailable), `SaveUI` (file I/O on the project folder layout, `<folder>/project.ecs3d` beside `assets/` and `scripts/`: Save As picks a folder, creates the subfolders, copies the previous folder's `assets/` and `scripts/` when the folder differs, and moves the editor's project root there, refusing a folder that already holds another project; Open takes a `.ecs3d` or legacy `.json` file, or a dropped folder, through `locateProject`; a legacy `.json` is saved in place until Save As; a template (see Build System's project root) is titled "Untitled (from" plus its name in the window title and its Save acts as Save As into a free or empty folder, prefilled under `Documents/ECS3D Projects`, refusing the template's own folder or one inside it (`ProjectOrigin.h`); the Assets panel's import and new-script actions ask `SaveUI::blockAssetWrites` first; every load and a root change reach `GpuAssetCache::setProjectRoot` through a callback; also owns the unsaved-changes gate - the edit counter and the perform/ignore/prompt decision are the headless `UnsavedChanges.h`: New/Open/a dropped file/closing the window all route through a guard that prompts Save/Don't Save/Cancel when a send-path callback has marked the project dirty since the last save/load - the window-close intercept sets its own raw `glfwSetWindowCloseCallback`, since `vke::Window` sets none; each successful open and save is added to the shared recent-projects list through `RecentProjectRecording.h`, except an untitled template, which waits for its first save to a folder; `forgetSaveTarget` drops the save file and folder when `ProjectLoadTracker.h` sees another editor's load), `GuiComponents` (whose numeric widgets refuse a non-finite ctrl-click entry, restoring the previous value and logging a warning, since a non-finite float serializes as json null), `Gizmo` (the headless translate/rotate/scale manipulator core - hit-testing and drag math over world-space line/triangle primitives it hands back for a backend to draw; pure glm, no ImGui/Vulkan, so it compiles straight into `ECS3DTests`), `GizmoRenderer` (the backend-neutral drawing interface `Gizmo.h`'s output is drawn through) / `ImGuiGizmoRenderer` (its only implementation today, drawing into the scene overlay's `ImDrawList`), `ViewportGizmo` (owns the `gizmo::State` and renderer for the current selection, reachable from `EditorApp`'s scene overlay callback - see Architecture's "Viewport gizmos"). The convex mesh collider handler (in `ColliderEditor`) takes a source model through the shared `AssetSlot`. Picking one, or pressing Rebuild hull, rebuilds the hull in the editor from the first mesh of the model in `GpuAssetCache`. The point-to-hull step is the headless `ConvexHullRebuild`. A failure, or a rebuild that changes nothing, logs at most a warning and reports no edit. Edits are whole-component, so every convex collider edit carries its whole hull, up to about 9 kilobytes. In a multi-object selection the hull keys `vertices`, `indices` and `sourceModel` travel together (`componentFieldDelta::withLinkedKeys`), a failing per-object apply is rolled back and skipped with one warning per gesture, and Rebuild hull is disabled while those keys differ. A model drop on such a selection still sends the edit, and an edit that leaves the primary unchanged propagates only its disagreeing hull keys. Depends on `ECS3DData` + `ECS3DRender` + `ECS3DSettings` + `nfd`. |
| `source/libs/net/` | `ECS3DNet` — `NetServer`/`NetClient`/`MessageQueue`/`ServerProcess`/`CommandLineSplit` (`ServerProcess`'s argument splitter for Linux and macOS)/`ClientConnectionState` (`NetClient`'s connected, lost and disconnect-requested flags)/`EditorConnections` (`NetServer`'s editor authorization and dropped-connection bookkeeping)/`ServerLog` (pack/unpack for `MessageType::serverLog`, the server's forwarded log — see Logging below) and `EditToken` (header-only `net::generateEditToken`, 128 bits from `std::random_device`) (C++), plus the `Transport/` C# assembly (`ECS3DNetTransport`, TCP + WebSocket backends). |
| `source/libs/scripting/` | `ECS3DScripting` — `ScriptSystem`, the `ScriptRuntime` interface it drives, `ScriptEngine` (its CLR (Common Language Runtime)-backed implementation, built by `ScriptEngineFactory`) + native `bindings/` (Transform, RigidBody, InputUtils, World, Camera, Collider, ModelRenderer, LightRenderer, PlayerController; `InputState`, `BindingContext`), plus the `ScriptBridge/` C# assembly (`ScriptBase` for object scripts, `SceneScript` for scene scripts, both over `ScriptInstance`) and example `UserScripts/`. |
| `source/libs/clrHost/` | `ECS3DClrHost` — `ManagedHost` boots CoreCLR and hands out managed statics as native fn ptrs. Owns the CMake helpers (`cmake/ECS3DManaged.cmake`, `FindDotnet.cmake`, `loadCS.cmake`). |
| `source/apps/` | The executables. `apps/CMakeLists.txt` orders them (server first — client/editor depend on it). |
| `source/apps/{server,client,editor}/` | The three C++ apps: a thin `main.cpp` (argv parsing) + a `*App` class. |
| `source/templates/` | Shipped project templates and samples, one folder each (`project.ecs3d`, `assets/`, `scripts/`, `template.json`, `thumbnail.png`). Its `README.md` documents the manifest, the uuid rules and the ground-tile convention. Copied to `<runtime output dir>/templates/` at configure time (`apps/server/CMakeLists.txt`, beside `defaultAssets`). `TemplateValidationTest` checks every folder here. |
| `source/apps/launcher/` | The standalone C# Avalonia launcher. **Has its own `AGENTS.md`** — treat it as an independent project. |
| `tools/shapes/` | Python generator, using only the standard library, for the generated default models and textures in `source/apps/server/defaultAssets/`. Its output is committed, so builds do not run it; the README records the ground-tile convention. `library.py` holds the convex shape library's vertex formulas, a small convex hull and the flat-shaded models. |
| `source/tests/` | `ECS3DTests` — the GoogleTest suite. Headless by construction (no window, GPU or server), registered with CTest. `tests/managed/` — `ECS3DManagedTests`, an xUnit project covering `ECS3DNetTransport`, also registered with CTest. Template fixtures live in `tests/fixtures/templates/`; `TemplateValidation.h` is the validator the template test uses. |
| `docs/guides/` | User-facing guides (editor basics, first script, scene scripts and events). Keep their claims true to the code when editor or scripting behavior changes. |
| `.github/workflows/` | `cmake-multi-platform.yml` — builds Release **and** Debug on Windows (MSVC), Linux (gcc+clang), macOS (clang) with the Vulkan SDK, then runs the suite through the `check` target; two more Linux rows (gcc, clang) run the `ecs3d-sanitize` preset's suite under ASan+UBSan, and one Linux gcc row runs `ecs3d-coverage` and uploads a gcovr/cobertura `coverage-report` artifact. A newer push to a pull request cancels that pull request's previous run. |

## Build System

- **CMake ≥ 3.29**, **C++23**. Executables land in `bin/` when ECS3D is the top-level project, which is
  also where `include(CTest)` runs.
- **Dependencies** (`FetchContent` in `source/libs/CMakeLists.txt`): nlohmann/json 3.12.0, glm 1.0.1,
  stduuid 1.2.3, nativefiledialog-extended (nfd) 1.3.0, and VulkanEngine (`main`). They are declared at
  the libs scope so every library links them directly. **glm is declared first, on purpose:**
  FetchContent is first-wins, and VulkanEngine also fetches glm — our pinned 1.0.1 must be the single
  copy both sides resolve to, unless VulkanEngine bumps glm and the tag here follows. **If VulkanEngine bumps glm, bump the tag here to match.**
- **dotnet 10** (the C# runtime and SDK) is required for the managed assemblies. `ecs3d_add_managed_assembly()` (in
  `clrHost/cmake/ECS3DManaged.cmake`) `dotnet publish`es a C# class lib next to the executables and
  writes its `runtimeconfig.json`; `ecs3d_deploy_clr_runtime()` copies `nethost.dll` beside each exe.
  `FindDotnet.cmake` warms up the SDK's one-time first-run configuration once at configure time (a
  single-threaded step that runs before any target's publish is scheduled), so no build-time `dotnet
  publish` races through it; the two engine managed assembly targets additionally depend on each other
  so they publish one at a time, while the launcher (`source/apps/launcher`) publishes independently.
- **Managed assemblies deployed at build time:** `ScriptBridge` → `bin/scripts/ScriptBridge`,
  `ECS3DNetTransport` → `bin/net/Transport`, user scripts → `bin/scripts/UserScripts`. All apps boot the
  CLR (Common Language Runtime) from the same runtime; the server loads `ScriptBridge` on top.
- **Runtime CWD (current working directory) = the executable's directory.** Asset paths, managed assembly paths
  and `nethost.dll` resolve relative to it. A model or texture path in a project resolves against the project
  root first, then against the executable's directory. Project roots, templates and the editor's save and import
  layout: [docs/architecture/project-layout.md](docs/architecture/project-layout.md).
- **Precompiled headers (PCH)** are set up in one block at the end of `source/CMakeLists.txt`: each
  multi-file ECS3D target gets its own PCH of the most-included standard and third-party headers.
  Targets differ in compile definitions (VulkanEngine adds the `GLM_FORCE_*`/`VULKAN_HPP_*` set), so
  they do not share one. `windows.h` and GLFW stay out, since four files define macros before including
  them. A new multi-file target adds itself to that block. Turning the `ECS3D_PRECOMPILED_HEADERS`
  option off disables it. The `ecs3d-sanitize` preset does, so CI still catches a missing include in the
  libraries the test suite builds, though not in render, editor, net, scripting or the apps. **Still
  include what you use** - the PCH is not a substitute.
- **Module scanning is off** (`CMAKE_CXX_SCAN_FOR_MODULES`, root `CMakeLists.txt`). Nothing uses C++20
  modules, and on MSVC the scan cost about an eighth of a clean build.
- **Source lists are explicit** in each lib's `CMakeLists.txt` (not globs). **Add new engine files to
  the owning library's list.**
- **Tests.** `ECS3DTests` (GoogleTest, `source/tests/`) is headless: it links `ECS3DData`, `ECS3DSim`,
  `ECS3DSettings` and `ECS3DNetProtocol`, not the renderer, the editor or `ECS3DNet`. `ECS3DManagedTests`
  (xUnit, `source/tests/managed/`) runs only through ctest. The `check` target builds and runs both, and CI runs
  it too. Fixtures, the sources compiled straight into the suite and the managed project's build isolation:
  [docs/architecture/testing.md](docs/architecture/testing.md).
- **Dependency direction (must hold unless a deliberate change updates this section):** `log` → nothing. `protocol` → nothing. `settings` → log (+ json). `data` →
  protocol + log (+ json/glm/uuid).
  `sim` → data. `render` → data + VulkanEngine. `editor` → data + render + settings + nfd + log. `net`/`scripting` →
  data + clrHost + log. `clrHost` → log. Apps compose these. **Do not add a Vulkan or ImGui include to `data`** (put such code in `render` or `editor` instead; lifting this takes a deliberate architecture decision recorded here) — that
  invariant is what keeps the headless server headless.

## Architecture Overview

**The data/systems split.** `ECS3DData` holds only *state* — component fields plus `serialize`/
`loadFromJSON`. Behavior lives in *systems* that operate on that data from the outside: `PhysicsSystem`/
`CollisionSystem` (sim), `RenderSystem` (render), `ScriptSystem` (scripting), the `*Editor` handlers
(editor). A component does not reach back into a manager or renderer; systems iterate
`ObjectManager::getAllObjects()` and pull the components they care about. `ComponentRegistry`
(populated by `registerDataComponents()`) is the type-name → factory table that deserialization uses,
so no layer needs to name concrete component types across the boundary.

**Client / server.** `ServerApp` is authoritative and headless — it is the **only** application that links
`ECS3DSim` + `ECS3DScripting` (the test suite also links `ECS3DSim`, to reach the collision math). It runs a fixed-timestep loop (`scriptSystem.variableUpdate` →
`fixedUpdate` → `physicsSystem` → `collisionSystem`) and streams state out. `ClientApp` renders + sends
input, linking `ECS3DRender` but not sim/scripting. `EditorApp` is a client plus the ImGui tooling
(`ECS3DEditorLib`); the authoritative scene lives on a spawned `--edit` server, so edits become
*commands sent back*, not local mutations. Client/editor spawn a child `ECS3DServer` via `ServerProcess`
for singleplayer (it has no console window by default; `--server-console` gives it one). The editor spawns it before creating its window and renderer, and the server builds its script runtime (the
user-script compile) before it starts listening, so that cost overlaps the editor's own startup rather than delaying the first snapshot.
**A loaded scene starts stopped:** a scene switch, a project load, and an `--edit` server's first load all
leave it stopped regardless of what the sim was doing before, until the editor presses play (a play server still auto-starts,
since it has no editor to press play).
**Stopping a scene discards every runtime change**, not just component values:
`SceneAsset::start()` snapshots the current object tree before the run, and `stop()` rebuilds it from
that snapshot with uuids preserved, undoing any script spawn/destroy/reparent (and any editor edit made
while playing) the same way it already undoes an edited Transform.

**Replication.** Structural state (project, scene, assets) travels as a full `ProjectPacker` snapshot, per-tick
motion as a binary `stateDelta`, and editor edits as typed commands (`editComponent`, `sceneEdit`,
`sceneControl`, `loadProject`, `addAsset`, `renameAsset`, `removeAsset`). Only a connection authorized as
`Role::editor` on an `--edit` server may send those commands. Script spawns and destroys use incremental
`objectSpawned`/`objectDestroyed` messages. Read
[docs/architecture/replication.md](docs/architecture/replication.md) before changing a wire payload or a
`sceneEdit` op.

**Prefabs.** A prefab is an `AssetRegistry` record whose body travels inline as one serialized `Object`.
Instantiating it assigns fresh uuids, and instances are detached copies. Detail:
[docs/architecture/prefabs.md](docs/architecture/prefabs.md).

**Transport / CLR.** `Protocol.h` defines the wire format, and C++ owns it. The C# `ECS3DNetTransport` owns the
sockets and moves opaque `(type byte, payload)` pairs. A connection's role is the one the transport granted at
the handshake, not one a message claims. Size limits, threading and shutdown ordering:
[docs/architecture/transport.md](docs/architecture/transport.md).

**Scripting.** `ScriptSystem` drives the C# `ScriptBridge` through the `ScriptRuntime` seam, so the suite tests
it against a fake. Native `bindings/` expose components to C# through function-pointer structs that C# mirrors
field for field, so add new fields at the **end** of both. Scripts reach neither the net layer nor `sim`
directly. Structural changes buffer on `BindingContext`, and sim queries arrive as injected function pointers.
Read [docs/architecture/scripting.md](docs/architecture/scripting.md) before changing the runtime, a binding or
the script lifecycle.

**Starter scripts.** Example gameplay scripts live in `UserScripts/Starters/`, one public class per file.
Detail: [docs/architecture/scripting.md](docs/architecture/scripting.md#starter-scripts).

**Script binding coverage.** Each binding provider exposes its component's full public surface except internal
bookkeeping. `bindings/BindingCoverage.h` fails the build for a component with no scripting decision. The
deliberate exclusions: [docs/architecture/script-bindings.md](docs/architecture/script-bindings.md).

**Logging.** An edit-mode server forwards its own log, scripts included, to editor connections only, as
`MessageType::serverLog` through `NetServer::sendToEditors`. The editor's Console panel shows those entries
prefixed `"[server] "`. Detail: [docs/architecture/logging.md](docs/architecture/logging.md).

**Contact clipping.** `sim/collisions/ContactManifold.h` builds contact points for two convex shapes from
plain vertex spans. Each shape's touching feature is the vertices within a tolerance of the extreme one along
the contact normal. The side with more points is the reference polygon; the other side is ordered into a
polygon, clipped to the reference's edge planes (Sutherland-Hodgman) and projected onto its plane. Features
with no area reduce to one point midway between their closest points.
A pair holding a convex mesh collider and a box or another hull takes its manifold from
`referenceFaceManifold` (`FaceManifold.h`), built the way the box pair is. Each shape offers its face normals
(`hullFaceNormals` for a hull, skipping slivers). The side whose face is more nearly parallel to the contact
normal is the reference, and alignments within `faceAlignmentTie` tie, going to the first collider. The box pair
uses the same band. The reference polygon is the convex hull of the touching vertices projected along the face.
The other side's touching vertices are clamped into it along the face plane, keeping their depth. So a
box-shaped hull makes the same `Contact` a box does. A side with no faces falls back to the clipping above.
A pair holding a capsule and a hull takes its manifold from that clipping in `Polytope`, at most
`Contact::points` wide, with the capsule's cap points as its feature. `PhysicsSystem` skips the box-axis flush
snap when a hull is in the pair, since a hull has no box faces to lie flush against.

**Camera.** `Camera` is a plain-field data component. `RenderSystem::updateCamera` drives the view and
projection from the active one and falls back to the editor's free-fly camera. A client finds its own camera
through its `PlayerController` slot. Input forwarding rules, including why the editor does not gate mouse input
on `io.WantCaptureMouse`: [docs/architecture/camera-and-input.md](docs/architecture/camera-and-input.md).

**Inertia.** `sim/InertiaTensor.h` computes each collider shape's world-space mass distribution: volume, centroid
and second moment per unit mass. It combines a body's colliders by volume share into one per-unit-mass inertia
tensor about the owner's position, with the parallel-axis theorem. `PhysicsSystem::worldInverseInertia` inverts it
for a body, so a sphere, capsule or hull spins and tips as its own shape does. A body with no usable collider falls
back to the box of its scale.

**Hull validation.** `ConvexHullValidation.h` holds `isValidConvexHull`, which checks a hull's structure but
not its convexity: vertex and index counts within the limits, finite vertices, every index naming a vertex.
`makeUnitCubeHull` is the default hull for a collider.

**Convex mesh collider.** The `ConvexMeshCollider` component (`data/objects/components/collisions/`) holds its
hull inline, as local-space vertices and triangle indices. The server therefore works without the model the
hull came from. Its `sourceModel` field only records where the hull was built. A fresh one is the unit cube
and places itself like a `BoxCollider` with the same offsets. Its `setHull`, `loadFromJSON` and `unpack`
refuse an invalid hull and change nothing. Before it allocates, `unpack` bounds each count. It caches its world
mass distribution, filled by `InertiaTensor`'s `hullOf` and keyed on a counter its world vertices bump when
they rebuild. The narrow phase, scene queries, scripts and the inspector all handle it, as their own sections
describe.

**Viewport gizmos.** The selected object's transform gizmo draws through the VulkanEngine scene overlay hook,
over the headless `gizmo::` core in `editor/Gizmo.{h,cpp}`. Picking runs from `EditorApp::run()`, not from the
overlay, and is Ctrl-gated. Detail:
[docs/architecture/viewport-gizmos.md](docs/architecture/viewport-gizmos.md).

**Editor Undo/Redo.** Undo sends a new reverse edit through the normal replication path rather than rewinding
locally, so the server stays the single source of truth. `data/edits/` holds the headless `EditHistory`, and
every editor mutation records its command before it sends. Read
[docs/architecture/undo-redo.md](docs/architecture/undo-redo.md) before adding an edit kind or a mutation path.

## Development Principles

- **Namespaces are the exception on ECS3D code** (unlike `vke::` in VulkanEngine), used where a name is
  generic enough that leaving it global would invite a collision: network code in `namespace net`, the
  GJK/EPA entry points in `namespace collisions` (`Contact`, `findContact`, `intersects`). Default to no
  namespace.
- **Headers:** `#ifndef NAME_H` include guards; forward-declare across libraries and include in the
  `.cpp` to keep coupling low (see the `*App.h` files — they forward-declare every collaborator).
- **Naming:** `PascalCase` types/files, `camelCase` methods/locals, `m_` member prefix. `[[nodiscard]]`
  on getters/queries; prefer `const` accessors.
- **Ownership:** subsystems are shared via `std::shared_ptr`, passed as `const std::shared_ptr<T>&`.
  `vke::raii` owns Vulkan handles — do not destroy them manually.
- **The serialize/loadFromJSON boundary is the contract.** Replication, save/load, and the registry all
  ride on it. When you add a component field, thread it through both — and only both; no layer should
  learn the concrete type.
- **A user preference is not project data.** Project state is authoritative and replicated to every client
  as a `ProjectPacker` snapshot, so anything stored there travels on the wire and is shared between users.
  Preferences are per-person and per-machine: they live in `ECS3DSettings` (`SettingsStore`, a JSON file
  under the per-user application data directory) and are kept out of a scene, a prefab, and a
  snapshot.
- **Respect the layer boundaries.** Put data in `data`, behavior in the matching system, UI in `editor`.
  Splitting a new component means: fields → `data`, physics → `sim`, rendering → `render`, bindings →
  `scripting`, inspector widget → `editor`. Register it in `registerDataComponents()`.
- **Source-list discipline:** every new file goes in its library's `CMakeLists.txt` source list.
- **Comments:** write them sparingly. Prefer self-documenting code (clear names, small functions) over a
  comment; if code needs a comment to be understood, first ask whether it can be made clearer instead.
  Comment only what the code cannot say for itself -- the *why* behind a non-obvious algorithm, design
  decision, workaround, or caveat -- not a restatement of *what* the line does. Keep them short (1-2
  lines). Do not narrate implementation history ("was X", "moved from Y", "Phase N", "temporary") or
  point at external plans/roadmaps -- describe the code as it is now. **Comments are ASCII (plain 7-bit text) only:** no
  em dashes, arrows, or other Unicode -- use `-`, `->`, `<->`.

## Applications

- **ECS3DServer** (`apps/server`) — headless authoritative sim. `main.cpp` parses `--project` (a project folder, its `project.ecs3d`, or a legacy `.json` file; the server records the root), `--port`,
  `--edit` (allow editor connections), `--open-edit` (`--edit` with no token required, development only),
  `--ephemeral` (exit when the last connection drops — a spawned local server), `--token`,
  `--log-file`/`--no-log-file`. Links Data+Sim+Scripting+Net+ClrHost+Log.
  Ships `defaultAssets/` (each file's source and license in `defaultAssets/LICENSES.md`) and generates a
  built-in `DefaultProject` when no `--project` is given. `DefaultShapes` builds the hulls of its Cylinder
  and Ramp prefabs from formula vertices through `buildConvexHull`. It also builds the nine shape library
  hulls (D4, D6, D8, D12, D20, Hex Prism, Drum, Gem, Boulder), whose formulas and seeded Boulder generator
  mirror `tools/shapes/library.py`, and a test pins coordinates from both. Also
  registers a `RemoteLogSink` with `Log` and forwards it to editor connections every `run()` iteration,
  only when `--edit` is set — see [docs/architecture/logging.md](docs/architecture/logging.md).
- **ECS3DClient** (`apps/client`) — the lightweight view. `--host`/`--port`/`--project` (folder or file, also passed to the spawned server)/`--console`/`--server-console`
  (opens a console window; Windows builds are GUI-subsystem and have none by
  default)/`--log-file`/`--no-log-file`. Links
  Data+Render+Net+ClrHost+Log. Spawns a local server for singleplayer; `--host` connects to an existing
  server instead.
- **ECS3DEditor** (`apps/editor`) — client + ImGui tooling (object tree, inspector, asset browser, scene
  controls, the viewport transform gizmo, save/load). `--host`/`--port`/`--project` (folder or file, also passed to the spawned server)/`--token` (edit token when attaching to an existing
  edit server)/`--console`/`--server-console` (`--console` opens a console window; Windows builds are GUI-subsystem and have none by
  default)/`--log-file`/`--no-log-file`. By default spawns its own `--edit` server with a generated
  token. Links Data+Render+EditorLib+Net+ClrHost+Log. It also registers a `RingBufferSink` for its Console panel, since
  it is the only app with a panel to show one.
- Each app's `main` registers a `ConsoleSink` with `Log` before anything else runs, then a `FileSink`
  writing to `<user data dir>/logs/<app>.log` (`editor`, `client` or `server`); `--log-file <path>`
  writes elsewhere and `--no-log-file` registers none. A server the editor or client spawns is told to
  log to `editor-server.log` / `client-server.log` instead, so two local servers do not share one file;
  the parent's `--log-file`/`--no-log-file` are not forwarded to it. All app, server and net
  (`ECS3DNet`) output goes through `Log`.
- An `--edit` server started without `--token` generates a random 128-bit token (`net::generateEditToken`, in
  `libs/net/EditToken.h`) and logs it once, so an editor can still attach with `--host`/`--token`. The editor
  uses the same generator for the server it spawns.
- `--open-edit` (development only) implies `--edit` and skips that generation, so any editor that reaches the
  port may edit. It logs a warning, and an explicit `--token` still applies.
- `net::ServerProcess::launch` takes the child's flags as one string; a token holding spaces may be
  double quoted (there is no escape for a quote inside one). POSIX (Linux and macOS) splits the string itself, honoring
  those quotes; Windows hands it to the child's CRT (C runtime), which parses them the same way.
- **ECS3DLauncher** (`apps/launcher`) — a standalone C# Avalonia project-management GUI. Independent of
  the C++ toolchain and the CLR-hosting path; built via `dotnet publish`. **See its own `AGENTS.md`.**

## AI Agent Guidelines

- Read this file before changing anything. When touching only the launcher, read
  `source/apps/launcher/AGENTS.md` instead — it stands alone.
- Understand the existing split (data vs. systems, client vs. authoritative server, snapshot vs.
  delta vs. command) before adding to it. Prefer **extending existing systems over parallel ones**.
- Do not add a Vulkan or ImGui dependency to `ECS3DData`, and do not make the server link
  render/editor — those invariants keep it headless.
- Add dependencies only via `source/libs/CMakeLists.txt` `FetchContent` (test-only deps belong in
  `source/tests/CMakeLists.txt`); register new files in the owning library's source list.
- **Do not build-verify the C++/CLR/Vulkan stack — the developer does that.**
- Do not invoke `cmake --build`.
- Do not run `dotnet build` on the C# projects.
- Do not run `dotnet publish` on the C# projects.
- Do not run `dotnet test` on `ECS3DManagedTests` yourself either — like the others, it breaks the CMake
  build with `CS0579` if its `BuildRoot` isolation is bypassed. Let `ctest`/`check` invoke it.
- Both break the CMake build with `CS0579`, so the C# projects are built through CMake (or, for the
  managed test project, ctest) only.
- State clearly that a change is unverified and needs to be compiled on the developer's machine.
- Avoid speculative refactors. Keep changes scoped and incremental. Ask about lifetime/ownership,
  threading, and replication semantics rather than assuming.
- Update this document when your understanding of the project meaningfully improves.

## Living Document Notice

- AGENTS.md is a **living document** and the primary onboarding reference for AI agents.
- Reflect significant architectural, structural, workflow, or convention changes here as they happen.
- Keep it **concise and relevant** rather than letting it grow into a full architecture manual. For deep
  topics, add a dedicated doc and link it here instead of expanding this file.
