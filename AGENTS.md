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
| `source/libs/log/` | `ECS3DLog` - the central log sink: the `Log` facade, its sinks (console, file, ring buffer, remote), `LogFilter` and the per-user data directory. Detail: [library reference](docs/architecture/library-reference.md#ecs3dlog). |
| `source/libs/protocol/` | `ECS3DNetProtocol` (INTERFACE lib): `Protocol.h` — the wire format (`MessageType`, `Message`/`MessageReader` binary framing, `Role`, ports). Depended on by everything that touches the wire. |
| `source/libs/settings/` | `ECS3DSettings` - per-user editor preferences (`SettingsStore`), the keybind model and the recent-projects list. **Not** project data: see Development Principles. Detail: [library reference](docs/architecture/library-reference.md#ecs3dsettings). |
| `source/libs/data/` | `ECS3DData` - the foundation: component data, `Object`/`ObjectManager`, scenes, assets, prefabs, JSON and wire serialization, replication payloads, the convex hull builder and the undo/redo model. **No Vulkan, no ImGui.** Has its own `AGENTS.md`. Detail: [library reference](docs/architecture/library-reference.md#ecs3ddata). |
| `source/libs/sim/` | `ECS3DSim` - `PhysicsSystem`, `CollisionSystem` (sweep-and-prune, then the GJK/EPA narrow phase under `collisions/`) and `SceneQueries`, operating on `ECS3DData` via accessors. Has its own `AGENTS.md`. Detail: [docs/architecture/physics.md](docs/architecture/physics.md). |
| `source/libs/render/` | `ECS3DRender` - `RenderSystem`, `GpuAssetCache` (uuid -> `vke` GPU objects), the gizmo meshes, `InputCapture` and headless helpers such as `Billboard.h`. Depends on `ECS3DData` + `VulkanEngine`. Has its own `AGENTS.md`. Detail: [library reference](docs/architecture/library-reference.md#ecs3drender). |
| `source/libs/editor/` | `ECS3DEditorLib` - the ImGui editing UI: object tree, inspector, asset browser, settings, console, scene scripts panel, save/load and the viewport gizmo. Depends on `ECS3DData` + `ECS3DRender` + `ECS3DSettings` + `nfd` + `ECS3DLog`. Has its own `AGENTS.md`. Detail: [docs/architecture/editor.md](docs/architecture/editor.md). |
| `source/libs/net/` | `ECS3DNet` — `NetServer`/`NetClient`/`MessageQueue`/`ServerProcess`/`CommandLineSplit` (`ServerProcess`'s argument splitter for Linux and macOS)/`ClientConnectionState` (`NetClient`'s connected, lost and disconnect-requested flags)/`EditorConnections` (`NetServer`'s editor authorization and dropped-connection bookkeeping)/`ServerLog` (pack/unpack for `MessageType::serverLog`, the server's forwarded log — see [docs/architecture/logging.md](docs/architecture/logging.md)) and `EditToken` (header-only `net::generateEditToken`, 128 bits from `std::random_device`) (C++), plus the `Transport/` C# assembly (`ECS3DNetTransport`, TCP + WebSocket backends). |
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

**Contact clipping.** `sim/collisions/ContactManifold.h` and `FaceManifold.h` build contact points for two
convex shapes. Detail: [docs/architecture/physics.md](docs/architecture/physics.md#contact-clipping).

**Camera.** `Camera` is a plain-field data component. `RenderSystem::updateCamera` drives the view and
projection from the active one and falls back to the editor's free-fly camera. A client finds its own camera
through its `PlayerController` slot. Input forwarding rules, including why the editor does not gate mouse input
on `io.WantCaptureMouse`: [docs/architecture/camera-and-input.md](docs/architecture/camera-and-input.md).

**Inertia, hull validation and the convex mesh collider.** `sim/InertiaTensor.h` gives each collider shape's
per-unit-mass inertia, and `ConvexMeshCollider` holds its hull inline so the server needs no model. Detail:
[docs/architecture/physics.md](docs/architecture/physics.md#inertia).

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
