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
  and **ECS3DLauncher** (a standalone C# Avalonia app — see `source/apps/launcher/AGENTS.md`).
- Scope today: a working editor + play/edit servers with physics (GJK and EPA, Gilbert-Johnson-Keerthi and Expanding Polytope Algorithm, collision; rigid bodies),
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
| `CMakeLists.txt` (root) | Top-level config: C++23, `bin/` output when top-level, `compile_commands.json`, `include(CTest)`, the `ECS3D_SANITIZE` option, MSVC (the Microsoft Visual C++ compiler) export-all-symbols. Then `add_subdirectory(source)`. |
| `CMakePresets.json` | Configure presets only: `ecs3d-debug`, `ecs3d-release`, `ecs3d-sanitize`, writing to `cmake-build-ecs3d-debug` / `-release` / `-sanitize`. `cmake --preset ecs3d-debug` then `cmake --build cmake-build-ecs3d-debug --target check` is the documented way in. |
| `source/libs/` | All reusable engine libraries. `libs/CMakeLists.txt` fetches shared deps (json, glm, uuid, nfd, VulkanEngine) and the managed-assembly helpers, then adds each lib. |
| `source/libs/log/` | `ECS3DLog` — a central log sink: `LogLevel`/`LogCategory` (+ `toString`), `LogEntry`, the `LogSink` interface, `ConsoleSink` (stdout/stderr, today's behavior), `RingBufferSink` (recent entries for the editor's console panel), `RemoteLogSink` (a bounded queue a server drains to forward its log to editor connections over the wire — see Logging below), `FileSink` (UTC-timestamped lines to a file, truncated per run), `LogFilter` (level/category toggles + a case-insensitive text search over a `LogEntry`, headless so it is testable without ImGui) and `formatEntry` (the shared `[level][category] message` rendering, prefixed with the time of day, used by the panel and its copy button), and the process-wide `Log` facade; `ConsoleWindow`'s `openConsoleWindow` allocates and attaches a console for GUI (graphical user interface) subsystem apps; `UserDataDirectory`'s `userDataDirectory`/`defaultLogFile` resolve the per-user, per-machine directory (settings and logs live there); `LogSetup`'s `addFileSinkFromArguments` registers an app's `FileSink` from its command line. Depends on nothing but the standard library and, on Windows, the console API (for `openConsoleWindow`). Apps register a `ConsoleSink` and a `FileSink` at startup. |
| `source/libs/protocol/` | `ECS3DNetProtocol` (INTERFACE lib): `Protocol.h` — the wire format (`MessageType`, `Message`/`MessageReader` binary framing, `Role`, ports). Depended on by everything that touches the wire. |
| `source/libs/settings/` | `ECS3DSettings` — `SettingsStore`, per-user editor preferences on disk (in the directory `ECS3DLog`'s `userDataDirectory` resolves), plus `Keybinds` (`KeyChord`/`parseChord`/`formatChord`, the `EditorAction` catalogue, and the bijective `KeybindTable`) - the headless keybind model, the numeric key/mod values of GLFW (the windowing library) spelled out as literals so this library still depends on nothing but json and ECS3DLog. **Not** project data: see Development Principles. |
| `source/libs/data/` | `ECS3DData` — the foundation. Component **data** (Transform, RigidBody, ModelRenderer, LightRenderer, Colliders, Script, PlayerController, Camera) - whose float and vec3 setters ignore non-finite input, since a non-finite float saves as json null and makes the file unreadable - `Object`/`ObjectManager`, scenes, `AssetRegistry` (incl. prefab bodies), `ComponentRegistry`, `ProjectSerializer` (JSON file save/load - a load that fails names the scene and the object it choked on) / `ProjectPacker` (binary wire snapshot), `Replication`, `edits/` (`EditCommand`/`EditHistory` — the undo/redo stack — plus `RecordEdits`, which derives the command for an edit the editor is about to send from its pre-edit replicated view; see Editor Undo/Redo below). **No Vulkan, no ImGui.** |
| `source/libs/sim/` | `ECS3DSim` — `PhysicsSystem` (integration, forces, response) and `CollisionSystem` (sweep-and-prune), calling the GJK/EPA narrow phase under `collisions/` — `NarrowPhase.h`'s `findContact`/`intersects` are its entry points. Operates on `ECS3DData` via accessors. OpenMP if available. |
| `source/libs/render/` | `ECS3DRender` — `RenderSystem` (draws models/lights, pick feedback, selection highlight, collider gizmos, and drives the `vke::Camera`/`Renderer3D` view from the scene's active `Camera` component), `GpuAssetCache` (UUID → `vke` GPU objects), `InputCapture`. Depends on `ECS3DData` + `VulkanEngine`. |
| `source/libs/editor/` | `ECS3DEditorLib` — ImGui editing UI: `ComponentEditor` (per-type handlers), `ObjectGUIManager` (object tree), `InspectorPanel` (the "Inspector" window — per-selection-kind dispatch) delegating the object kind to `ObjectInspector` (which, beside the per-frame value send, coalesces a whole continuous edit - a slider drag mutates the component in place every frame - into one before/after pair reported once no widget is active) and the asset kind to `AssetInspector` (per-`AssetType` views — read-only detail plus a display-name rename field and a delete button with a reference-count warning for the flat file assets; the **Prefab body is editable** — deserialized into a detached `TransientObject` and edited by a reused `ObjectInspector`, see Prefabs), `EditorSelection` (shared kind-tagged selection slot holding an ordered set of uuids rather than a single one, `Selection.h` - the back of the list is the primary, what the Inspector/gizmo show; a plain click in the tree or viewport replaces it, Ctrl-click toggles membership, and Shift-click in the tree selects the contiguous range between an anchor uuid and the clicked row in the tree's current on-screen order, which `ObjectGUIManager` tracks itself each frame since collapsed nodes and the sort mode both affect what "on screen order" means), `SettingsPanel` (the "Settings" window — a section nav beside the selected section's content, reading and writing `ECS3DSettings` directly since preferences are local, not replicated; Appearance edits the `EditorTheme.h` palette tokens live; Keybinds lists every `EditorAction`, its current chord, and Rebind/Unbind/Reset controls, driving `KeybindDispatcher`'s capture mode and showing a conflict modal - naming the holding action, no reassign option - when a rebind targets an already-held chord), `KeybindDispatcher` (the one `vke::KeyCallbackEvent` listener that resolves a press against a `KeybindTable` and calls the registered handler, suppressed while ImGui wants the keyboard; also drives the Settings panel's rebind capture), `ConsolePanel` (the "Console" window — reads the editor's `RingBufferSink` through a `LogFilter`: level and category toggles with per-level counts, a text search, Copy (the filtered rows through `formatEntry` to the clipboard), Clear (remembers a sequence number rather than mutating the shared sink), and Auto-scroll; entries the connected server forwards land in this same sink, message-prefixed `"[server] "`, so they show and filter alongside the editor's own — see Logging below), `AssetBrowserPanel`, `AssetDisplay` (shared asset label/name/icon/color rules, header-only), `SaveUI` (also owns the unsaved-changes gate: New/Open/a dropped file/closing the window all route through a guard that prompts Save/Don't Save/Cancel when a send-path callback has marked the project dirty since the last save/load - the window-close intercept sets its own raw `glfwSetWindowCloseCallback`, since `vke::Window` sets none), `GuiComponents` (whose numeric widgets refuse a non-finite ctrl-click entry, restoring the previous value and logging a warning, since a non-finite float serializes as json null). Depends on `ECS3DData` + `ECS3DRender` + `ECS3DSettings` + `nfd`. |
| `source/libs/net/` | `ECS3DNet` — `NetServer`/`NetClient`/`MessageQueue`/`ServerProcess`/`ServerLog` (pack/unpack for `MessageType::serverLog`, the server's forwarded log — see Logging below) (C++), plus the `Transport/` C# assembly (`ECS3DNetTransport`, TCP + WebSocket backends). |
| `source/libs/scripting/` | `ECS3DScripting` — `ScriptSystem`/`ScriptEngine` + native `bindings/` (Transform, RigidBody, InputUtils, World, Camera, ModelRenderer, LightRenderer; `InputState`, `BindingContext`), plus the `ScriptBridge/` C# assembly and example `UserScripts/`. |
| `source/libs/clrHost/` | `ECS3DClrHost` — `ManagedHost` boots CoreCLR and hands out managed statics as native fn ptrs. Owns the CMake helpers (`cmake/ECS3DManaged.cmake`, `FindDotnet.cmake`, `loadCS.cmake`). |
| `source/apps/` | The executables. `apps/CMakeLists.txt` orders them (server first — client/editor depend on it). |
| `source/apps/{server,client,editor}/` | The three C++ apps: a thin `main.cpp` (argv parsing) + a `*App` class. |
| `source/apps/launcher/` | The standalone C# Avalonia launcher. **Has its own `AGENTS.md`** — treat it as an independent project. |
| `source/tests/` | `ECS3DTests` — the GoogleTest suite. Headless by construction (no window, GPU or server), registered with CTest. |
| `.github/workflows/` | `cmake-multi-platform.yml` — builds Release **and** Debug on Windows (MSVC), Linux (gcc+clang), macOS (clang) with the Vulkan SDK, then runs the suite through the `check` target. |

## Build System

- **CMake ≥ 3.29**, **C++23**. Executables land in `bin/` when ECS3D is the top-level project, which is
  also where `include(CTest)` runs.
- **Dependencies** (`FetchContent` in `source/libs/CMakeLists.txt`): nlohmann/json 3.12.0, glm 1.0.1,
  stduuid 1.2.3, nativefiledialog-extended (nfd) 1.3.0, and VulkanEngine (`main`). They are declared at
  the libs scope so every library links them directly. **glm is declared first, on purpose:**
  FetchContent is first-wins, and VulkanEngine also fetches glm — our pinned 1.0.1 must be the single
  copy both sides resolve to. **If VulkanEngine bumps glm, bump the tag here to match.**
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
- **Runtime CWD (current working directory) = the executable's directory.** Asset paths (`assets/models/...`), managed assembly
  paths (`net/Transport/...`, `scripts/...`), and `nethost.dll` are all resolved relative to it. The
  server's `defaultAssets/` are copied into `bin/assets/` at configure time.
- **Source lists are explicit** in each lib's `CMakeLists.txt` (not globs). **Add new engine files to
  the owning library's list.**
- **Test fixtures** live in `source/tests/TestScene.h` (namespace `fixtures`). `fixtures::Scene`
  opens an `ObjectManager` over a `ComponentRegistry`, registering the data components in it unless the
  `Components::none` constructor argument says otherwise - the setup every suite used to repeat - and the
  free functions beside it add objects, colliders and rigid bodies, and compare `glm::vec3` with a
  tolerance and a trace. Derive from `fixtures::Scene` to hang extra members off a scene. Only
  `addObject`/`addChildObject` are reachable unqualified, by ADL (argument-dependent lookup) through their `Scene` argument;
  everything else takes arguments that do not name `fixtures` (or, for `makeScene`, none at all), so it
  needs `fixtures::` or a using-declaration. **Build a scene through these rather than re-deriving the scaffolding in a new suite.**
- **Tests** (`source/tests/`) build as `ECS3DTests`, linking `ECS3DData`, `ECS3DSim`, `ECS3DSettings` and
  `ECS3DNetProtocol` — never the renderer, the editor or `ECS3DNet` — so the suite stays runnable without a
  window, GPU or server. `net/MessageQueue.cpp` is compiled straight into the target rather than linked,
  because it is the one piece of `ECS3DNet` with no CLR dependency; see the comment in the test
  `CMakeLists.txt` before adding more. It builds into `<build-dir>/tests`, not `bin/`. GoogleTest is fetched in
  `tests/CMakeLists.txt` rather than with the shared deps, and the directory is gated on
  `PROJECT_IS_TOP_LEVEL` and `BUILD_TESTING` together — `BUILD_TESTING` is a cache variable a parent project may
  already have set, so the top-level check is what actually keeps an embedded ECS3D from fetching
  GoogleTest. `gtest_discover_tests` registers every case with CTest, and the `check` target builds the
  suite and runs it: `cmake --build <build-dir> --target check`. That target is what CI runs too, so a
  defect in it is caught rather than shipped; it passes `--no-tests=error`, since ctest exits 0 on an
  empty test set and would otherwise report green for a suite that registered nothing.
- **Dependency direction (must hold):** `log` → nothing. `protocol` → nothing. `settings` → log (+ json). `data` →
  protocol + log (+ json/glm/uuid).
  `sim` → data. `render` → data + VulkanEngine. `editor` → data + render + settings + nfd + log. `net`/`scripting` →
  data + clrHost + log. `clrHost` → log. Apps compose these. **`data` must never gain a Vulkan or ImGui include** — that
  invariant is what keeps the headless server headless.

## Architecture Overview

**The data/systems split.** `ECS3DData` holds only *state* — component fields plus `serialize`/
`loadFromJSON`. Behavior lives in *systems* that operate on that data from the outside: `PhysicsSystem`/
`CollisionSystem` (sim), `RenderSystem` (render), `ScriptSystem` (scripting), the `*Editor` handlers
(editor). A component never reaches back into a manager or renderer; systems iterate
`ObjectManager::getAllObjects()` and pull the components they care about. `ComponentRegistry`
(populated by `registerDataComponents()`) is the type-name → factory table that deserialization uses,
so no layer needs to name concrete component types across the boundary.

**Client / server.** `ServerApp` is authoritative and headless — it is the **only** application that links
`ECS3DSim` + `ECS3DScripting` (the test suite also links `ECS3DSim`, to reach the collision math). It runs a fixed-timestep loop (`scriptSystem.variableUpdate` →
`fixedUpdate` → `physicsSystem` → `collisionSystem`) and streams state out. `ClientApp` renders + sends
input, linking `ECS3DRender` but never sim/scripting. `EditorApp` is a client plus the ImGui tooling
(`ECS3DEditorLib`); the authoritative scene lives on a spawned `--edit` server, so edits become
*commands sent back*, not local mutations. Client/editor spawn a child `ECS3DServer` via `ServerProcess`
for singleplayer (`--no-server-console` launches it without a console window; the default shows one).
**Stopping a scene discards every runtime change**, not just component values:
`SceneAsset::start()` snapshots the current object tree before the run, and `stop()` rebuilds it from
that snapshot with uuids preserved, undoing any script spawn/destroy/reparent (and any editor edit made
while playing) the same way it already undoes an edited Transform.

**Replication.** Two paths. Structural state (project/scene/assets) goes as a full **snapshot** — a
packed binary blob built by `ProjectPacker` (the wire counterpart of `ProjectSerializer`, which remains
the JSON path for file save/load) — sent on join and rebroadcast after any structural edit; every view
rebuilds from it, atomically (a malformed packet leaves the current project intact). Per-tick motion
goes as a compact binary **stateDelta** (uuid + local transform per object; `data/Replication.{h,cpp}`).
Edits flow the other way as typed commands (`editComponent`, `sceneEdit`, `sceneControl`, `loadProject`,
`addAsset`, `renameAsset`, `removeAsset`) that only a connection authorized as `Role::editor` on an
`--edit` server may send. A third direction exists for `serverLog` alone: server → editor connections
only, unicast via `NetServer::sendToEditors` rather than broadcast to every connection — see Logging below. An `editComponent` a replicated view could not apply is reported through
`replication::logMissedComponentEdit` — debug for an absent object/component (routine, since the server
rebroadcasts to views that may be a round trip behind), error for a malformed or partially applied
payload (a real divergence). (`sceneEdit` carries the prefab-instantiation op too — see Prefabs below.)
`sceneEdit` also carries two ops for undoing a deletion: `removeSubtree` deletes an object and its whole
subtree immediately, promoting nothing, unlike `removeObject` (which defers to the next tick and promotes
the removed object's children); `restoreObject` rebuilds a subtree from an inline serialized body under a
parent at a sibling index, and is the one structural op that **preserves the body's uuids** rather than
reassigning them, because the undo history (`data/edits/EditCommand.h`) already names the removed
subtree's objects by those uuids. The
three `sceneEdit` ops that create an object (`addObject`, `duplicateObject`, `instantiatePrefab`) may carry
a client-chosen `"uuid"` for what they create; the server honors it and picks its own when the field is
absent. A uuid that does not parse is a `malformedEdit`; the nil uuid, or one already in use, is
`rejected` with the scene untouched. It exists so the sender knows which object its own edit produced -
the undo history records the reverse edit against that uuid.
`reparentObject` rewrites the moved object's local transform after reattaching so its world placement is
unchanged, and `ObjectManager::deleteObjectsMarkedForDeletion` applies the same `objects/WorldPlacement.h`
helper to the children of a deleted object as they move up a level (and promotes them into the deleted
object's own slot, preserving their relative order, rather than appending them after whatever already
followed it there).
`reorderObject` is `reparentObject`'s drop-BETWEEN-siblings counterpart: it carries a sibling index
alongside the (optional) parent, so an object can land at a specific position rather than at the end of
the list, and covers both a same-parent reorder and a move-to-a-different-parent-at-an-index in the one op.
The index is read against the target list **after** the object is removed from wherever it sits now; an
index past that list's end is `rejected` rather than clamped, and so is one that would cycle or leave the
object exactly where it already was. `Object::addChild`/`ObjectManager::addObjectToRoot` each have an
index-taking overload (clamping to the list's size) that both `reorderObject` and `restoreObject` build on.
The
asset-mutation trio (`addAsset`/`renameAsset`/`removeAsset`, built/packed in `data/Replication.{h,cpp}`,
applied by `AssetRegistry`) all follow the **local-apply-then-send** shape: the editor mutates its own
registry for instant feedback, then sends the op and the server re-snapshots. **Rename is display-only** —
a `renameAsset` sets an optional `AssetRecord::displayName` override (threaded through
`serialize`/`loadFromJSON`/`pack`/`unpack` like every other field); the file on disk and `path` (the
registry key, and the name-key for prefabs/scenes) never change. **Delete always succeeds and references
dangle** — `removeAsset` drops the record; `GpuAssetCache`/`AssetRegistry` lookups already null-tolerate a
missing uuid so referencing slots just show "None". The editor warns before deleting by scanning its
replicated scenes + prefab bodies for the uuid ("referenced by N objects"); no server-side refusal or
cascade exists (a known gap, not yet scheduled). Rename/delete are offered only for the flat file assets
(Model/Texture/Script/Prefab) that `AssetRegistry` owns — a Scene record is regenerated from the
`SceneManager` on every snapshot, so an override on it wouldn't survive.
Runtime structural changes from a *script* (spawn/destroy) take a third path: lightweight
`objectSpawned` (one packed `Object`) / `objectDestroyed` (a uuid) messages the client splices into/out of
its scene incrementally — kept off the full-snapshot path so frequent spawning stays cheap. Build/apply
live in `data/Replication.{h,cpp}`; a late joiner still gets the objects via the normal join snapshot.

**Prefabs.** A prefab is an `AssetRegistry` record whose **body travels inline** — one `Object::serialize()`
blob, stored dumped in `AssetRecord::body` and threaded through the same `serialize`/`loadFromJSON`/`pack`/
`unpack` contract as everything else (the shape `Script::m_fields` already uses). It is deliberately **not a
file on disk**: a model path names a *client-side GPU resource the server never touches*, but a prefab body
is *server-side gameplay data the server must have to instantiate*, and the editor and server may share no
filesystem. Prefabs (like scenes) key off a display name in `path`; `registerAsset` is first-wins for every
other type, but **re-registering an existing prefab name updates its body in place, keeping the uuid**, so
"Save as Prefab" over an existing name means *update it*. Instantiation is
`ObjectManager::instantiate(body)` — fresh uuids via `reassignUUIDs`, the shared core of `duplicateObject` —
reachable two ways: the editor's `instantiatePrefab` **`sceneEdit` op** (the one op keyed by an asset, hence
the `const AssetRegistry*` parameter on `applySceneEdit`; it takes an optional parent uuid, so dropping a
prefab onto an object in the hierarchy instantiates it as that object's child via `instantiateUnder` instead
of at the scene root), and a script's `World.spawnPrefab(uuid, position)`,
which rides 1.3's existing `objectSpawned` replication (one message carries the whole subtree). The binding
reaches the registry through **`BindingContext::setAssetRegistry`**, injected once at startup exactly like the
sim's raycast/overlap statics. **Instances are detached copies** — nothing records which prefab an object came
from, so overrides and prefab→instance propagation don't exist (deliberately deferred).
`DefaultProject` defines its `Block`/`Rigid Block`/`Sphere`/`Player` bodies once, registers them as prefabs
with stable uuids, and builds its scenes by instancing them. **Editing a prefab's contents** happens in the
Inspector: `AssetInspector` deserializes the body into a `TransientObject` (editor lib) — a detached `Object`
living in a private scratch `ObjectManager` never wired to a scene/`SceneManager`/replication, uuids
preserved so the body is stable across edits — and draws it with a reused `ObjectInspector`. Its edits apply
locally (value edits in place; structural edits deferred past `display()` then `applySceneEdit`'d, so the
component map isn't mutated mid-iteration), and each change re-serializes the body and re-registers it under
the prefab's existing name via the `addAsset` op (updating the body in place, keeping the uuid — the same
"Save as Prefab" path), which re-snapshots. `TransientObject::markSynced` records the just-sent body so the
local-apply/snapshot echo doesn't rebuild the object mid-edit. Because that body update makes the server
re-snapshot the **whole project** (unlike a live object's cheap `editComponent` rebroadcast), the send is
**coalesced**: edits mutate the detached object live for instant local feedback but only flush to the network
once no ImGui widget is active (drag released) or the selection changes, so a whole slider drag is one
snapshot rather than one per frame.

**Transport / CLR.** `Protocol.h` defines the format. `Message::write`/`MessageReader::read` take
integers, enums, `float` and `double` only, and carry `bool` as an explicit 0/1 byte; an aggregate goes
across whole only by specializing `net::wirePackable`, which is a claim - assert the layout - that it has
neither padding nor a pointer in it. `data`'s `WireTypes.h` holds those specializations, for `glm::vec3`
and `uuids::uuid`, and any translation unit that reasons about the trait rather than just calling `write`/`read` includes
it, since otherwise it evaluates the primary template and disagrees with the rest of the program. Widths and byte order are still the caller's problem: pack fixed-width types, and the wire
carries host endianness. `NetServer`/`NetClient` own the format in C++ and hand `ECS3DNetTransport` (C#)
opaque `(type byte, payload)` pairs. Both transports refuse an inbound message over
`TransportBackend.MaxMessageBytes` and drop the connection - TCP on the length the peer declares,
WebSocket on what has actually arrived, since a fragmented message declares none. Neither backend sizes
its read buffer to that declared length up front: TCP grows a buffer to roughly what has actually arrived
(`TcpBackend.ReadBody` starts at 64 KiB and doubles each time the buffer fills, up to the declared
length) and WebSocket
grows its assembly buffer as fragments arrive, so a peer that declares a large frame and then trickles it
in a byte at a time pins only a small multiple of what has actually landed, not the whole declared size.
TCP backs that with a per-read progress timeout (`BodyReadTimeoutMs`, reset on every read rather than
covering the whole frame) so a body that stalls outright still drops just that connection. The handshake,
the one
message read before a peer is authorized, gets the much smaller `MaxHandshakeBytes`. Oversize *outbound*
messages are refused at the sender, where there is something useful to say about them. Object nesting has
its own limit alongside the byte ones: `maxObjectDepth` (`data/objects/Object.h`) caps how deep
`Object::unpack`/`loadChildren` and `ObjectManager::reassignUUIDs` will recurse into a wire or JSON
payload, so a tree claiming more depth than any real hierarchy needs is refused rather than exhausting the
stack. A lost client connection is reported the same way: the transport calls
`Transport.DeliverClientDisconnect()` once its client receive loop exits, reaching `NetClient`
(`clientSetDisconnectCallback`) so the client and editor apps can surface it on screen. Before that call,
the loop also releases the connection's own resources (socket, or `WebSocketBackend`'s `ClientConnection`
bundle of socket, `HttpMessageInvoker` and `CancellationTokenSource`) instead of relying on a later
`ClientDisconnect` call, which the apps don't make on their own after a lost-connection notice. The loop
works from the instance it was started with: it clears the backing field with a compare-and-swap against
that instance before disposing it, so a connection a concurrent `ClientConnect` has already installed in
its place is left alone, and `ClientSend` guards its own read of the field with a matching
compare-and-swap plus an `ObjectDisposedException` catch for the same reason. The exiting loop sets
`_clientRunning` false as soon as it stops reading, ahead of that cleanup, so a fast reconnect can start
before the old loop finishes tearing down; the loop compares what the compare-and-swap reports the field
held against its own connection before delivering the notice, and skips delivery when that turns out to be
a different, live connection a concurrent `ClientConnect` already installed, so a reconnect racing the old
loop's exit is not reported as a loss.
The role a connection is actually granted at the handshake (`TransportBackend.Authorize`) is
reported to C++ separately from the messages it sends: once `Authorize` succeeds, both backends call
`Transport.DeliverServerAuthorized(connId, role)`, which reaches `NetServer::authorize` and is remembered
in `NetServer::isEditor`. `ServerApp::handleClientMessage` enforces `net::isMutationMessage(type)` against
that authorized role (in addition to edit-mode), never against a role a message merely claims - a
connection cannot mutate an edit-mode server's scene by sending a mutation type unless the transport
actually granted it `Role::editor`. `ManagedHost`
boots CoreCLR and resolves
managed statics as native function pointers; inbound frames are pushed from C# socket threads into a
thread-safe `MessageQueue` and drained by the app loop. The transport backend (TCP/WebSocket) is
selected by a single field in `Transport.cs`. The transport logs through a native `setLogCallback`
(`net::transportLog`, category `net`), falling back to the console before it is registered. A client
connect attempt is bounded to `ConnectTimeoutMs` in each backend so the apps' retry loops keep their
deadline instead of hanging on the OS connect timeout. Both backends
broadcast from a snapshot of the connection list taken under the lock and send outside it, under one
`SendTimeoutMs` budget shared by the whole fan-out, so a peer that stops reading disconnects only itself
instead of stalling the tick thread. A connection is dropped only when its own send failed or timed out;
peers the broadcast ran out of budget before reaching just miss that one message.
Shutdown joins every socket thread it started before returning: `ServerStop` flips `_serverRunning` and
takes its snapshot of started threads in the same lock the accept loop checks that flag and registers a
new thread under, so a thread that starts is one this call goes on to join; it then closes the listener
and all connection sockets and joins the accept thread and each per-connection thread.
`ClientDisconnect` closes the connection then joins the receive thread the same way. Each join is bounded
by `ShutdownJoinTimeoutMs`, logging a warning if a thread has not exited by then instead of waiting past
it, and skips a thread that is the caller (joining that thread would deadlock). `NetServer::stop`/
`NetClient::disconnect` rely on that join completing before they clear the `g_activeServer`/
`g_activeClient` pointer the native callbacks read, so by the time a `NetServer`/`NetClient`'s teardown
clears that pointer, no socket thread remains that could still call back into it; both pointers are
`std::atomic` because they are written on the app thread and read on the socket threads with no other
synchronization between them.

**Scripting.** `ScriptSystem` drives `ScriptBridge` (C# gameplay scripts) through `ManagedHost`. Native
`bindings/` expose Transform/RigidBody/InputUtils/World to C# via fn-ptr structs; each fn-ptr struct is
mirrored by a C# `[StructLayout(Sequential)]` struct and registered through `Bridge` (add new fields at
the **end** of both to keep the layout matched). `BindingContext` is the bridge from the static, C-ABI (application binary interface)
bindings back to the server's live scene: `ScriptSystem` points it at the current `ObjectManager` each
tick. The headless server has no GLFW window, so input is networked: clients send `inputState`, the
server writes it into `InputState`, and `InputUtilsBindings` reads it back for scripts. Input is
**per-player**: the transport tags each inbound message with a stable connection id (`NetServer::poll`'s
`senderId`, from a monotonic id the C# backends assign); `ServerApp` binds each connection to a **player
slot** on join (freed on disconnect via a transport disconnect callback + `NetServer::takeDisconnected`),
and `InputState` is keyed by that slot. A script reads *its own* player through `ScriptBase.input`
(`PlayerInput`), which resolves `object → PlayerController.playerSlot → InputState[slot]`; the global
`InputUtils` stays a player-agnostic aggregate. `PlayerController` is an ordinary replicated data
component (slot is editable + serialized), so possession is visible to the editor and to clients — the
hook the camera uses to pick "whose view" (see Camera below). Mouse delta/scroll and key edges
(`wasPressedThisTick`) accumulate between fixed ticks and are reset per tick by
`InputState::clearMouseDeltas()`/`commitInputEdges()`. Forces
requested from a script are buffered on the `RigidBody` data (pending-force queue) and drained by
`PhysicsSystem`, keeping `scripting` independent of `sim`. Five conventions worth inheriting: (1) reaching
another object's component is a **`tryGet`** (`World.tryGetTransform(uuid, out t)` → false when
absent/destroyed; never throws in the tick loop) — future component wrappers follow this; (2) a binding
that mutates scene structure can't touch the net layer, so it **buffers the change on `BindingContext`**
and the app drains + replicates it after the tick (see the spawn/destroy path in Replication above).
Spawning is also deferred a level lower, in `ObjectManager` itself: `ScriptSystem::fixedUpdate`/
`variableUpdate` range over `getAllObjects()` and run script code inside the loop, so `addObject` cannot
append straight to that live vector without risking a reallocation mid-iteration. `ScriptSystem` holds an
`ObjectManager::ScriptPassGuard` around each of those loops; while one is alive, `addObject` queues the new
object instead (still immediately parented/started/`getObjectByUUID`-able) and `flushPendingAdditions`
splices it into `m_allObjects`/`m_objects` once the pass ends - called from `ServerApp::
broadcastStructuralChanges` at the same point `deleteObjectsMarkedForDeletion` drains a removal, so a
script-spawned object joins the scene the same way a script-destroyed one leaves it: after the pass, not
mid-iteration; (3) **sim→script events cross at the app, as plain data.** `CollisionSystem` records each tick's colliding
pairs and diffs them into enter/stay/exit uuid-pair lists; `ServerApp` hands those to
`ScriptSystem::dispatchCollisionEvent` (→ `onCollisionEnter/Stay/Exit` script virtuals) after the collision
pass — the same "buffer plain data, let the app carry it" shape as pending forces, so `sim` never links
`scripting`; (4) **sim→script *queries* cross by function-pointer injection.** Query behavior stays in a
`sim` system (`SceneQueries`: raycast/overlapSphere), and `ServerApp` injects its statics into
`BindingContext` (`setRaycast`/`setOverlapSphere`) at startup; the `World` bindings call through them. The
injected signatures use only shared types (`ObjectManager`/`glm`/`uuid`), so no library learns the other —
the pattern to reuse for any future sim query exposed to scripts; (5) **a read-only script binding for
data the object doesn't own outright** follows the same `Transform`/`RigidBody` shape even when nothing
mutates it: `CameraBindings` (`getDirection`/`has`) lets `PlayerScript` read its own `Camera` component so
movement can be relative to wherever the camera actually faces, degrading to the forward default
`(0,0,-1)` when the object has none — the same "safe missing-component" convention as `tryGet`, just
without the `tryGet` ceremony since `ScriptBase` always constructs one for the script's own object (like
`transform`/`rigidBody`/`input`). `bindings/BindingCoverage.h` holds a table of every `ComponentType`
against bound/notYetBound/nativeOnly; a new enumerator with no row fails the build, so adding a component
without deciding its scripting story can't go unnoticed. Script field edits are validated
(`ScriptFieldEdit.h`) against the instance's exposed fields before any setter runs; a mismatched field is
refused with a warning and does not fault the script. `LogBindings` gives scripts `Log.trace/debug/
info/warn/error` through `ECS3DLog` under `LogCategory::script`, registered first in `registerBindings` so
everything the bridge itself logs afterwards - init, hot-reload, compilation - already has a binding to
write through. Instance lifetime tracks the live component set in both directions: a Script added to a
running scene is attached and started by the next tick, and the mirror image holds too - a script removed
mid-run (or whose object is destroyed) is stopped and detached by an orphan sweep that runs on every tick
and on every scene edit's snapshot. The sweep matches by component identity, not by (uuid, class) key, so
a re-added script of the same class gets a fresh instance rather than inheriting the stale one.

**Logging.** The server is headless, so its own log (and, via `LogBindings`, the scripts running on it) is
forwarded to connected editors rather than only reaching its console window/log file. `ServerApp` registers
a `RemoteLogSink` with `Log` alongside its file/console sinks; each `run()` iteration (not gated on a fixed
tick, so a line still gets out while the scene is stopped/paused) drains it - capped by both entry count and
an estimated byte size (`RemoteLogSink::drain`'s two limits; a single oversized message is truncated with a
marker at `write()` time, so one huge entry can neither dominate the sink's memory nor stall the queue for
everyone behind it) - and forwards what comes out as a `MessageType::serverLog` (`net::packServerLog`/
`unpackServerLog`) via `NetServer::sendToEditors`, skipping the send entirely when there is nothing queued
and no one to send it to. That copies out the current editor connection ids under `m_editorMutex` and
releases the lock before calling the transport, then makes one call (`Transport.serverSendToMany`, alongside
`serverBroadcast`) for the whole batch of editor connections rather than one call per editor, so a stalled
editor connection costs this send one shared time budget no matter how many editors are connected - the same
guarantee `ServerBroadcast` already gives its own per-connection sends across every connection. A
per-connection loop on the authoritative tick thread would instead let K stalled editors block physics and
state deltas for K times that budget. Only connections authorized as `Role::editor` are named, so play
clients do not receive it. `RemoteLogSink::drain` also reports how many entries it had to evict (queue full, nobody
draining fast enough); the editor surfaces that count as a warning instead of silently missing history.
`EditorApp::handleServerLog` writes each forwarded entry straight into its own `RingBufferSink` (bypassing
`Log::write`, which would otherwise stamp it with arrival time instead of the time it carried on the wire),
message-prefixed `"[server] "` so it reads apart from the editor's own logging in the same `LogCategory`
(both sides log under `net`, for instance) without collapsing every entry into one category and losing the
Console panel's level/category filters. This works for a server the
editor spawned *and* one it only connected to (`--host`) — it rides the same connection, not a pipe to a
child process. The separate `--console`/`showServerConsole` window some launches show is unrelated and
keeps behaving exactly as before.

**Camera.** `Camera` is a plain-field data component (`direction`, `fov`, `nearPlane`, `farPlane`,
`active`) — position comes from the object's `Transform`, so only the *look* needs its own field.
`RenderSystem::updateCamera` finds the active `Camera` (optionally restricted to one object), builds
`lookAt(pos, pos + q·direction, worldUp)` — `q` is the object's orientation, so the camera turns as the
object turns, but `worldUp` (not an orientation-derived up) keeps the horizon level — then disables
`vke::Camera`'s free-fly and calls `Renderer3D::setCameraParameters`; no active camera re-enables free-fly
(`RenderSystem::useFreeFlyCamera`, which also pushes the free-fly pose once on the handover — `render()`
only pushes it while the scene view is focused, so without that the component camera's last pose lingers).
The editor's **View** combo (Scene Status) picks what its viewport looks through: its own free-fly camera
(the default) or any object in the scene carrying a `Camera`, which is how you see a client's view — a
player camera is labelled with its `PlayerController` slot. A stale choice (object gone, `Camera` removed)
falls back to free-fly. **FOV (field of view)/near/far are
carried and editable but have no visible effect yet** — `vke`'s projection matrix is hardcoded
(`RenderInfo::getProjectionMatrix`); a fix needs an upstream `VulkanRenderer` change (a projection setter
alongside `setCameraParameters`), deliberately deferred. A client picks its
*own* camera via the player↔object association (`PlayerController.playerSlot` + a `Camera` on the same
object) using a **nonce-over-broadcast** handshake: the client tags its `join` with a random nonce, the
server echoes `(nonce, slot)` back over the existing broadcast (`NetServer` has no targeted-send path), and
only the client whose nonce matches keeps it — chosen over adding a targeted-send ABI to the C# transport,
which would have meant touching both backends for one bit of routing. `PlayerScript` mouse-look rotates the
object's `Transform` from `input.mouseDelta()` while right-click is held (matching the free-fly camera's own
gesture) and zeroes `RigidBody` angular velocity each tick so a collision-induced spin can't fight the look;
movement is relative to the `Camera.direction` (via the binding above) rotated by that yaw, not a hardcoded
forward axis. **The editor must not gate forwarded mouse input on `io.WantCaptureMouse`**: its 3D viewport
*is* an ImGui window under the dockspace, so that flag is set whenever the cursor is over the scene, and
gating on it silently swallows the right-drag mouse-look. `EditorApp::captureGatedInput` (called from `sendInput`) instead forwards the mouse
only while the viewport looks through a scene camera (in free-fly the right-drag belongs to the editor's own
camera, so forwarding it too would turn the player at the same time) **and** `RenderingManager::isSceneFocused()`
is true — the same signal `vke` gates its free-fly camera on. The keyboard still gates on
`io.WantCaptureKeyboard`, which only trips for text input, so WASD (the movement keys) reaches the game from either view.

**Editor Undo/Redo.** `data/edits/EditCommand.h` and `EditHistory.h` hold the undo/redo stack. It is
headless by design (it links `ECS3DData` and nothing UI-side). The design decision that shapes it: **undo is a new edit, not a
local rewind** - undoing sends an ordinary reverse edit back through the normal replication path and waits
for the rebroadcast like any other change, so the server stays the single source of truth and every
connected view converges the same way. A command records its target uuid(s) and a before/after state in
replicated form (a component's `serialize()` blob, an object's name/parent, a whole removed subtree, a
whole asset record); the payload to send is built on demand from `Replication.h`'s own builders, never
stored as a built `net::Message` (a `Command` stays a plain, copyable, comparable value that way).
**Validation happens once, at the moment of undo/redo - never on every snapshot**, which would invalidate
the whole stack on every structural edit even in single-user editing. It compares the command's
after-state (undo) or before-state (redo) against the live scene/registry; a mismatch refuses the whole
edit and drops that entry and everything older still on the stack being popped (undo is sequential -
skipping a dropped entry to reach an older one would apply reverts out of order), naming which uuid
conflicted. Not every kind is reversible with the sceneEdit ops that exist today: `removeObject`/
`removeComponent` would need a one-shot "recreate with this data" op that does not exist, and
`duplicateObject`/`instantiatePrefab` create a whole subtree that `ObjectManager::removeObject` cannot
cleanly undo (it reparents children up rather than deleting them) - those kinds are still representable in
the history but report `notUndoable` instead of sending a lossy or structurally wrong reverse. **Every
editor mutation is recorded**: each of `EditorApp`'s mutation callbacks (component edit, scene edit, add
asset, rename/remove asset) asks `edits/RecordEdits.h` for the command before it sends, deriving the
before state from the replicated view while that view still holds it, and records what comes back; an edit
nothing faithful can be derived for (a stale view, an op with no matching kind) is logged at debug and
skipped rather than refused. The `replaceAsset` kind exists for that recording: an `addAsset` over a uuid - or a
prefab name - the registry already holds replaces a record rather than adding one (a prefab body edit,
"Save as Prefab" over an existing name, which mints a fresh uuid the name-keyed registry then discards),
and `addAsset`'s reverse would delete the prefab instead of restoring its previous body. Both stacks are cleared wherever the authored scene they refer to is replaced: load project
(New/Open), scene switch, (re)connect, and a play start or stop - a pause/resume is not one, since it
leaves the scene as it is.

`EditorApp::undo()`/`redo()` are what read the stacks back: each peeks the top of the relevant stack with
`EditHistory::nextUndoKind()`/`nextRedoKind()` before calling `undo()`/`redo()`, and only proceeds for a
`componentEdit` - a value edit is the one kind this editor currently sends the reverse of. A structural or
asset kind on top is refused with a "not yet undoable" log message and left in place rather than handed to
`EditHistory::undo()`/`redo()`, which would otherwise treat "a kind this caller does not attempt" the same
as a validation conflict and drop it (and everything older beneath it) even though nothing about it is
actually wrong. `EditCommand`/`EditHistory` already build and validate a faithful reverse for every
reversible kind (the round trip is exercised in `EditHistoryRoundTripTest.cpp` for all of them), so extending
`EditorApp::undo()`/`redo()` to structural and asset kinds is a matter of widening that one kind check, not
new library work. There is still no menu item or keybind that calls `undo()`/`redo()` - a later story wires
one in; for now they exist as entry points only. A refusal is logged the same way for a stale target
(`targetMissing`/`targetChanged`, naming the conflicting uuid) or an empty stack. `EditHistory::
reportUndoRejected()`/`reportRedoRejected()` exist for a server rejection of the edit undo()/redo() just
sent, but nothing calls them yet: today's `editComponent`/`sceneEdit` handling has no wire-level
acknowledgement back to the sender for a rejected edit to hook into (a failed edit is only logged
server-side, and sometimes answered with a resync snapshot) - see `ServerApp::handleSceneEdit`/
`handleEditComponent`.

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
  `vke::raii` owns Vulkan handles — never manually destroy.
- **The serialize/loadFromJSON boundary is the contract.** Replication, save/load, and the registry all
  ride on it. When you add a component field, thread it through both — and only both; no layer should
  learn the concrete type.
- **A user preference is not project data.** Project state is authoritative and replicated to every client
  as a `ProjectPacker` snapshot, so anything stored there travels on the wire and is shared between users.
  Preferences are per-person and per-machine: they live in `ECS3DSettings` (`SettingsStore`, a JSON file
  under the per-user application data directory) and are never serialized into a scene, a prefab, or a
  snapshot.
- **Respect the layer boundaries.** Put data in `data`, behavior in the matching system, UI in `editor`.
  Splitting a new component means: fields → `data`, physics → `sim`, rendering → `render`, bindings →
  `scripting`, inspector widget → `editor`. Register it in `registerDataComponents()`.
- **Source-list discipline:** every new file goes in its library's `CMakeLists.txt` source list.
- **Comments:** write them sparingly. Prefer self-documenting code (clear names, small functions) over a
  comment; if code needs a comment to be understood, first ask whether it can be made clearer instead.
  Comment only what the code cannot say for itself -- the *why* behind a non-obvious algorithm, design
  decision, workaround, or caveat -- never a restatement of *what* the line does. Keep them short (1-2
  lines). Do not narrate implementation history ("was X", "moved from Y", "Phase N", "temporary") or
  point at external plans/roadmaps -- describe the code as it is now. **Comments are ASCII (plain 7-bit text) only:** no
  em dashes, arrows, or other Unicode -- use `-`, `->`, `<->`.

## Applications

- **ECS3DServer** (`apps/server`) — headless authoritative sim. `main.cpp` parses `--project`, `--port`,
  `--edit` (allow editor connections), `--ephemeral` (exit when the last connection drops — a spawned
  local server), `--token`, `--log-file`/`--no-log-file`. Links Data+Sim+Scripting+Net+ClrHost+Log.
  Ships `defaultAssets/` and generates a built-in `DefaultProject` when no `--project` is given. Also
  registers a `RemoteLogSink` with `Log` and forwards it to editor connections every `run()` iteration —
  see Logging above.
- **ECS3DClient** (`apps/client`) — the lightweight view. `--host`/`--port`/`--project`/`--console`
  (opens a console window; Windows builds are GUI-subsystem and have none by
  default)/`--log-file`/`--no-log-file`. Links
  Data+Render+Net+ClrHost+Log. Spawns a local server for singleplayer; `--host` connects to an existing
  server instead.
- **ECS3DEditor** (`apps/editor`) — client + ImGui tooling (object tree, inspector, asset browser, scene
  controls, save/load). `--host`/`--port`/`--project`/`--token` (edit token when attaching to an existing
  edit server)/`--console` (opens a console window; Windows builds are GUI-subsystem and have none by
  default)/`--log-file`/`--no-log-file`. By default spawns its own `--edit` server with a generated
  token. Links Data+Render+EditorLib+Net+ClrHost+Log. It also registers a `RingBufferSink` for its Console panel, since
  it is the only app with a panel to show one.
- Each app's `main` registers a `ConsoleSink` with `Log` before anything else runs, then a `FileSink`
  writing to `<user data dir>/logs/<app>.log` (`editor`, `client` or `server`); `--log-file <path>`
  writes elsewhere and `--no-log-file` registers none. A server the editor or client spawns is told to
  log to `editor-server.log` / `client-server.log` instead, so two local servers never share one file;
  the parent's `--log-file`/`--no-log-file` are not forwarded to it. All app, server and net
  (`ECS3DNet`) output goes through `Log`.
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
- Both break the CMake build with `CS0579`, so the C# projects are built through CMake only.
- State clearly that a change is unverified and needs to be compiled on the developer's machine.
- Avoid speculative refactors. Keep changes scoped and incremental. Ask about lifetime/ownership,
  threading, and replication semantics rather than assuming.
- Update this document when your understanding of the project meaningfully improves.

## Living Document Notice

- AGENTS.md is a **living document** and the primary onboarding reference for AI agents.
- Reflect significant architectural, structural, workflow, or convention changes here as they happen.
- Keep it **concise and relevant** rather than letting it grow into a full architecture manual. For deep
  topics, add a dedicated doc and link it here instead of expanding this file.
