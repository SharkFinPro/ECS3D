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
- Scope today: a working editor + play/edit servers with physics (GJK/EPA collision, rigid bodies),
  C# scripting, asset import, scene management, and full snapshot/delta replication over TCP.

## Repository Structure

| Path | Responsibility |
|------|----------------|
| `CMakeLists.txt` (root) | Top-level config: C++23, `bin/` output when top-level, MSVC export-all-symbols. Just `add_subdirectory(source)`. |
| `source/libs/` | All reusable engine libraries. `libs/CMakeLists.txt` fetches shared deps (json, glm, uuid, nfd, VulkanEngine) and the managed-assembly helpers, then adds each lib. |
| `source/libs/protocol/` | `ECS3DNetProtocol` (INTERFACE lib): `Protocol.h` — the wire format (`MessageType`, `Message`/`MessageReader` binary framing, `Role`, ports). Depended on by everything that touches the wire. |
| `source/libs/data/` | `ECS3DData` — the foundation. Component **data** (Transform, RigidBody, ModelRenderer, LightRenderer, Colliders, Script, PlayerController, Camera), `Object`/`ObjectManager`, scenes, `AssetRegistry` (incl. prefab bodies), `ComponentRegistry`, `ProjectSerializer` (JSON file save/load) / `ProjectPacker` (binary wire snapshot), `Replication`. **No Vulkan, no ImGui.** |
| `source/libs/sim/` | `ECS3DSim` — `PhysicsSystem` (integration, forces, response) and `CollisionSystem` (sweep-and-prune + GJK/EPA under `collisions/`). Operates on `ECS3DData` via accessors. OpenMP if available. |
| `source/libs/render/` | `ECS3DRender` — `RenderSystem` (draws models/lights, pick feedback, selection highlight, collider gizmos, and drives the `vke::Camera`/`Renderer3D` view from the scene's active `Camera` component), `GpuAssetCache` (UUID → `vke` GPU objects), `InputCapture`. Depends on `ECS3DData` + `VulkanEngine`. |
| `source/libs/editor/` | `ECS3DEditorLib` — ImGui editing UI: `ComponentEditor` (per-type handlers), `ObjectGUIManager` (object tree), `InspectorPanel` (the "Inspector" window — per-selection-kind dispatch) delegating the object kind to `ObjectInspector` and the asset kind to `AssetInspector` (read-only per-`AssetType` views), `EditorSelection` (shared kind-tagged selection slot, `Selection.h`), `AssetBrowserPanel`, `AssetDisplay` (shared asset label/name/icon/color rules, header-only), `SaveUI`, `GuiComponents`. Depends on `ECS3DData` + `ECS3DRender` + `nfd`. |
| `source/libs/net/` | `ECS3DNet` — `NetServer`/`NetClient`/`MessageQueue`/`ServerProcess` (C++), plus the `Transport/` C# assembly (`ECS3DNetTransport`, TCP + WebSocket backends). |
| `source/libs/scripting/` | `ECS3DScripting` — `ScriptSystem`/`ScriptEngine` + native `bindings/` (Transform, RigidBody, InputUtils, World, Camera; `InputState`, `BindingContext`), plus the `ScriptBridge/` C# assembly and example `UserScripts/`. |
| `source/libs/clrHost/` | `ECS3DClrHost` — `ManagedHost` boots CoreCLR and hands out managed statics as native fn ptrs. Owns the CMake helpers (`cmake/ECS3DManaged.cmake`, `FindDotnet.cmake`, `loadCS.cmake`). |
| `source/apps/` | The executables. `apps/CMakeLists.txt` orders them (server first — client/editor depend on it). |
| `source/apps/{server,client,editor}/` | The three C++ apps: a thin `main.cpp` (argv parsing) + a `*App` class. |
| `source/apps/launcher/` | The standalone C# Avalonia launcher. **Has its own `AGENTS.md`** — treat it as an independent project. |
| `.github/workflows/` | `cmake-multi-platform.yml` — builds Release on Windows (MSVC), Linux (gcc+clang), macOS (clang) with the Vulkan SDK, then `ctest`. |

## Build System

- **CMake ≥ 3.29**, **C++23**. Executables land in `bin/` when ECS3D is the top-level project.
- **Dependencies** (`FetchContent` in `source/libs/CMakeLists.txt`): nlohmann/json 3.12.0, glm 1.0.1,
  stduuid 1.2.3, nativefiledialog-extended (nfd) 1.3.0, and VulkanEngine (`main`). They are declared at
  the libs scope so every library links them directly. **glm is declared first, on purpose:**
  FetchContent is first-wins, and VulkanEngine also fetches glm — our pinned 1.0.1 must be the single
  copy both sides resolve to. **If VulkanEngine bumps glm, bump the tag here to match.**
- **.NET 10** is required for the managed assemblies. `ecs3d_add_managed_assembly()` (in
  `clrHost/cmake/ECS3DManaged.cmake`) `dotnet publish`es a C# class lib next to the executables and
  writes its `runtimeconfig.json`; `ecs3d_deploy_clr_runtime()` copies `nethost.dll` beside each exe.
- **Managed assemblies deployed at build time:** `ScriptBridge` → `bin/scripts/ScriptBridge`,
  `ECS3DNetTransport` → `bin/net/Transport`, user scripts → `bin/scripts/UserScripts`. All apps boot the
  CLR from the same runtime; the server loads `ScriptBridge` on top.
- **Runtime CWD = the executable's directory.** Asset paths (`assets/models/...`), managed assembly
  paths (`net/Transport/...`, `scripts/...`), and `nethost.dll` are all resolved relative to it. The
  server's `defaultAssets/` are copied into `bin/assets/` at configure time.
- **Source lists are explicit** in each lib's `CMakeLists.txt` (not globs). **Add new engine files to
  the owning library's list.**
- **Dependency direction (must hold):** `protocol` → nothing. `data` → protocol (+ json/glm/uuid).
  `sim` → data. `render` → data + VulkanEngine. `editor` → data + render + nfd. `net`/`scripting` →
  data + clrHost. Apps compose these. **`data` must never gain a Vulkan or ImGui include** — that
  invariant is what keeps the headless server headless.

## Architecture Overview

**The data/systems split.** `ECS3DData` holds only *state* — component fields plus `serialize`/
`loadFromJSON`. Behavior lives in *systems* that operate on that data from the outside: `PhysicsSystem`/
`CollisionSystem` (sim), `RenderSystem` (render), `ScriptSystem` (scripting), the `*Editor` handlers
(editor). A component never reaches back into a manager or renderer; systems iterate
`ObjectManager::getAllObjects()` and pull the components they care about. `ComponentRegistry`
(populated by `registerDataComponents()`) is the type-name → factory table that deserialization uses,
so no layer needs to name concrete component types across the boundary.

**Client / server.** `ServerApp` is authoritative and headless — it is the **only** thing that links
`ECS3DSim` + `ECS3DScripting`. It runs a fixed-timestep loop (`scriptSystem.variableUpdate` →
`fixedUpdate` → `physicsSystem` → `collisionSystem`) and streams state out. `ClientApp` renders + sends
input, linking `ECS3DRender` but never sim/scripting. `EditorApp` is a client plus the ImGui tooling
(`ECS3DEditorLib`); the authoritative scene lives on a spawned `--edit` server, so edits become
*commands sent back*, not local mutations. Client/editor spawn a child `ECS3DServer` via `ServerProcess`
for singleplayer.

**Replication.** Two paths. Structural state (project/scene/assets) goes as a full **snapshot** — a
packed binary blob built by `ProjectPacker` (the wire counterpart of `ProjectSerializer`, which remains
the JSON path for file save/load) — sent on join and rebroadcast after any structural edit; every view
rebuilds from it, atomically (a malformed packet leaves the current project intact). Per-tick motion
goes as a compact binary **stateDelta** (uuid + local transform per object; `data/Replication.{h,cpp}`).
Edits flow the other way as typed commands (`editComponent`, `sceneEdit`, `sceneControl`, `loadProject`,
`addAsset`) that only a connection authorized as `Role::editor` on an `--edit` server may send. (`sceneEdit`
carries the prefab-instantiation op too — see Prefabs below.)
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
the `const AssetRegistry*` parameter on `applySceneEdit`), and a script's `World.spawnPrefab(uuid, position)`,
which rides 1.3's existing `objectSpawned` replication (one message carries the whole subtree). The binding
reaches the registry through **`BindingContext::setAssetRegistry`**, injected once at startup exactly like the
sim's raycast/overlap statics. **Instances are detached copies** — nothing records which prefab an object came
from, so overrides and prefab→instance propagation don't exist (deliberately deferred; see `ROADMAP.md`).
`DefaultProject` defines its `Block`/`Rigid Block`/`Sphere`/`Player` bodies once, registers them as prefabs
with stable uuids, and builds its scenes by instancing them.

**Transport / CLR.** `Protocol.h` defines the format; `NetServer`/`NetClient` own it in C++ and hand
`ECS3DNetTransport` (C#) opaque `(type byte, payload)` pairs. `ManagedHost` boots CoreCLR and resolves
managed statics as native function pointers; inbound frames are pushed from C# socket threads into a
thread-safe `MessageQueue` and drained by the app loop. The transport backend (TCP/WebSocket) is
selected by a single field in `Transport.cs`.

**Scripting.** `ScriptSystem` drives `ScriptBridge` (C# gameplay scripts) through `ManagedHost`. Native
`bindings/` expose Transform/RigidBody/InputUtils/World to C# via fn-ptr structs; each fn-ptr struct is
mirrored by a C# `[StructLayout(Sequential)]` struct and registered through `Bridge` (add new fields at
the **end** of both to keep the layout matched). `BindingContext` is the bridge from the static, C-ABI
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
and the app drains + replicates it after the tick (see the spawn/destroy path in Replication above);
(3) **sim→script events cross at the app, as plain data.** `CollisionSystem` records each tick's colliding
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
`transform`/`rigidBody`/`input`).

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
falls back to free-fly. **FOV/near/far are
carried and editable but have no visible effect yet** — `vke`'s projection matrix is hardcoded
(`RenderInfo::getProjectionMatrix`); a fix needs an upstream `VulkanRenderer` change (a projection setter
alongside `setCameraParameters`), tracked as deliberately deferred in `ROADMAP.md`. A client picks its
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
gating on it silently swallows the right-drag mouse-look. `EditorApp::sendInput` instead forwards the mouse
only while the viewport looks through a scene camera (in free-fly the right-drag belongs to the editor's own
camera, so forwarding it too would turn the player at the same time) **and** `RenderingManager::isSceneFocused()`
is true — the same signal `vke` gates its free-fly camera on. The keyboard still gates on
`io.WantCaptureKeyboard`, which only trips for text input, so WASD reaches the game from either view.

## Development Principles

- **No namespace on ECS3D code** (unlike `vke::` in VulkanEngine). Network code lives in `namespace net`.
- **Headers:** `#ifndef NAME_H` include guards; forward-declare across libraries and include in the
  `.cpp` to keep coupling low (see the `*App.h` files — they forward-declare every collaborator).
- **Naming:** `PascalCase` types/files, `camelCase` methods/locals, `m_` member prefix. `[[nodiscard]]`
  on getters/queries; prefer `const` accessors.
- **Ownership:** subsystems are shared via `std::shared_ptr`, passed as `const std::shared_ptr<T>&`.
  `vke::raii` owns Vulkan handles — never manually destroy.
- **The serialize/loadFromJSON boundary is the contract.** Replication, save/load, and the registry all
  ride on it. When you add a component field, thread it through both — and only both; no layer should
  learn the concrete type.
- **Respect the layer boundaries.** Put data in `data`, behavior in the matching system, UI in `editor`.
  Splitting a new component means: fields → `data`, physics → `sim`, rendering → `render`, bindings →
  `scripting`, inspector widget → `editor`. Register it in `registerDataComponents()`.
- **Source-list discipline:** every new file goes in its library's `CMakeLists.txt` source list.

## Applications

- **ECS3DServer** (`apps/server`) — headless authoritative sim. `main.cpp` parses `--project`, `--port`,
  `--edit` (allow editor connections), `--ephemeral` (exit when the last connection drops — a spawned
  local server), `--token`. Links Data+Sim+Scripting+Net+ClrHost. Ships `defaultAssets/` and generates a
  built-in `DefaultProject` when no `--project` is given.
- **ECS3DClient** (`apps/client`) — the lightweight view. `--host`/`--port`/`--project`. Links
  Data+Render+Net+ClrHost. Spawns a local server for singleplayer; `--host` connects to an existing
  server instead.
- **ECS3DEditor** (`apps/editor`) — client + ImGui tooling (object tree, inspector, asset browser, scene
  controls, save/load). `--host`/`--port`/`--project`/`--token` (edit token when attaching to an existing
  edit server). By default spawns its own `--edit` server with a generated token. Links
  Data+Render+EditorLib+Net+ClrHost.
- **ECS3DLauncher** (`apps/launcher`) — a standalone C# Avalonia project-management GUI. Independent of
  the C++ toolchain and the CLR-hosting path; built via `dotnet publish`. **See its own `AGENTS.md`.**

## AI Agent Guidelines

- Read this file before changing anything. When touching only the launcher, read
  `source/apps/launcher/AGENTS.md` instead — it stands alone.
- Understand the existing split (data vs. systems, client vs. authoritative server, snapshot vs.
  delta vs. command) before adding to it. Prefer **extending existing systems over parallel ones**.
- Do not add a Vulkan or ImGui dependency to `ECS3DData`, and do not make the server link
  render/editor — those invariants keep it headless.
- Add dependencies only via `source/libs/CMakeLists.txt` `FetchContent`; register new files in the
  owning library's source list.
- **Do not build-verify the C++/CLR/Vulkan stack — the developer does that.** Don't invoke `cmake --build`,
  and never `dotnet build`/`publish` the C# projects directly. State clearly that a change is unverified
  and needs to be compiled on the developer's machine.
- Avoid speculative refactors. Keep changes scoped and incremental. Ask about lifetime/ownership,
  threading, and replication semantics rather than assuming.
- Update this document when your understanding of the project meaningfully improves.

## Living Document Notice

- AGENTS.md is a **living document** and the primary onboarding reference for AI agents.
- Reflect significant architectural, structural, workflow, or convention changes here as they happen.
- Keep it **concise and relevant** rather than letting it grow into a full architecture manual. For deep
  topics, add a dedicated doc and link it here instead of expanding this file.
