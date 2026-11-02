# AGENTS.md

> **Living document, loaded at the start of every agent session.** It holds what every change needs: the
> safeguards, the shape of the engine and where the rest of the knowledge lives. Area rules and design notes live
> in the files the [documentation map](#documentation-map) lists, and [Keeping the docs current](#keeping-the-docs-current)
> says where new knowledge goes. `AgentDocsTest` holds this file to a size budget.

## Project Overview

- **ECS3D** is a 3D game engine built around a data-oriented Entity Component System (ECS), rendered by the
  **VulkanEngine** library (the sibling project [SharkFinPro/VulkanRenderer](https://github.com/SharkFinPro/VulkanRenderer),
  fetched by CMake `FetchContent` at tag `main`).
- The engine is **authoritative client/server multiplayer** by design. A headless server owns the simulation;
  clients render a replicated view and send input. Singleplayer is the same server spawned as a local child
  process. This is the shape of the whole codebase, not an add-on.
- C++ owns the engine, the data and the wire protocol. **C# runs in-process on CoreCLR**, the Common
  Language Runtime (CLR) hosted through nethost/hostfxr, for gameplay scripting (`ScriptBridge`) and for the
  network transport (`ECS3DNetTransport`), which owns the sockets.
- Four executables ship: **ECS3DServer**, **ECS3DClient**, **ECS3DEditor** (all C++) and **ECS3DLauncher**, a
  standalone C# Avalonia app with [its own AGENTS.md](source/apps/launcher/AGENTS.md).
- Scope today: an editor plus play and edit servers, rigid-body physics with Gilbert-Johnson-Keerthi (GJK) and
  Expanding Polytope Algorithm (EPA) collision, C# scripting, asset import, scene management, and full
  snapshot/delta replication over the Transmission Control Protocol (TCP).

## Role and Tools

- **Role:** you are a coding agent working for the ECS3D developer on this repository, extending the engine
  described above while following the conventions below.
- **Tools:** the project builds with CMake presets (`CMakePresets.json`) and tests with GoogleTest (native) and
  xUnit (managed) through CTest's `check` target. The developer runs builds and tests, not the agent; see
  AI Agent Guidelines.

## AI Agent Guidelines

- Read this file first. Before changing an area, read its own `AGENTS.md` and the design note the
  [documentation map](#documentation-map) names for it. Claude Code loads a folder's `AGENTS.md` once it reads a
  file in that folder, but a search alone does not load it, so open one file there before planning.
- When touching only the launcher, read `source/apps/launcher/AGENTS.md` instead - it stands alone.
- **The developer builds and runs the tests, not the agent.** Do not invoke `cmake --build`, and do not run
  `dotnet build`, `dotnet publish` or `dotnet test` on any C# project here. A direct `dotnet` run writes a
  project-local `obj/` that breaks the next CMake build with error CS0579; the C# projects build through CMake and
  the managed tests through ctest. State clearly that a change is unverified and needs compiling on the
  developer's machine.
- Understand the existing split (data vs. systems, client vs. authoritative server, snapshot vs. delta vs.
  command) before adding to it. Prefer **extending existing systems over parallel ones**.
- Avoid speculative refactors. Keep changes scoped and incremental. Ask about lifetime and ownership, threading and
  replication semantics rather than assuming.
- Adding a `CLAUDE.md` or `CLAUDE.local.md` at the repository root makes Claude Code read it instead of this file by
  default. Put shared instructions here, and personal ones in `~/.claude/CLAUDE.md`, which loads alongside it.

## Repository Structure

| Path | Responsibility |
|------|----------------|
| `.clang-format`, `.editorconfig` | The intended style: 2-space indent, 120 columns, braces on their own line for functions and control flow. **The tree has not been reformatted to match them yet**, so read them as intent. |
| `CMakeLists.txt` (root) | C++23, `bin/` output when top-level, `include(CTest)`, the `ECS3D_SANITIZE`, `ECS3D_COVERAGE` and `ECS3D_PROFILE` options, Microsoft Visual C++ (MSVC) export-all-symbols. |
| `CMakePresets.json` | Configure presets `ecs3d-debug`, `-release`, `-sanitize`, `-coverage`, `-profile`, each writing to `cmake-build-<preset>`. `cmake --preset ecs3d-debug` then `cmake --build cmake-build-ecs3d-debug --target check` is the documented way in. |
| `source/libs/` | The engine libraries. `libs/CMakeLists.txt` fetches the shared dependencies and the managed-assembly helpers, then adds each library. |
| `source/libs/log/` | `ECS3DLog` - the `Log` facade, its sinks (console, file, ring buffer, remote), `LogFilter` and the per-user data directory. |
| `source/libs/profile/` | `ECS3DProfile` - scope timers recorded into per-thread lanes and a sample history. Depends on nothing. |
| `source/libs/protocol/` | `ECS3DNetProtocol` (interface library): `Protocol.h`, the wire format (`MessageType`, `Message`/`MessageReader`, `Role`, ports). |
| `source/libs/settings/` | `ECS3DSettings` - per-user editor preferences (`SettingsStore`), the keybind model and the recent-projects list. **Not** project data. |
| `source/libs/data/` | `ECS3DData` - component data, `Object`/`ObjectManager`, scenes, assets, prefabs, serialization, replication payloads, the convex hull builder and the undo/redo model. **No Vulkan, no ImGui.** |
| `source/libs/sim/` | `ECS3DSim` - `PhysicsSystem`, `CollisionSystem` (sweep-and-prune, then the GJK/EPA narrow phase under `collisions/`) and `SceneQueries`. |
| `source/libs/render/` | `ECS3DRender` - `RenderSystem`, `GpuAssetCache` (uuid -> `vke` GPU objects), gizmo meshes and headless render helpers. |
| `source/libs/editor/` | `ECS3DEditorLib` - the ImGui editing UI: object tree, inspector, asset browser, settings, console, scene scripts panel, save/load and the viewport gizmo. |
| `source/libs/net/` | `ECS3DNet` - `NetServer`/`NetClient`, `MessageQueue`, `ServerProcess`, editor authorization and server-log packing (C++), plus the `Transport/` C# assembly with TCP and WebSocket backends. |
| `source/libs/scripting/` | `ECS3DScripting` - `ScriptSystem` over the `ScriptRuntime` seam, its CLR-backed `ScriptEngine`, the native `bindings/`, the `ScriptBridge/` C# assembly and the example `UserScripts/`. |
| `source/libs/clrHost/` | `ECS3DClrHost` - `ManagedHost` boots CoreCLR and hands out managed statics as native function pointers. Owns the CMake helpers (`cmake/ECS3DManaged.cmake`, `FindDotnet.cmake`). |
| `source/apps/{server,client,editor}/` | The three C++ apps: a thin `main.cpp` for argument parsing plus a `*App` class. `apps/CMakeLists.txt` builds the server first. |
| `source/apps/launcher/` | The standalone C# Avalonia launcher. **Has its own `AGENTS.md`** - treat it as an independent project. |
| `source/templates/` | Shipped project templates and samples, one folder each. Its `README.md` documents the manifest, the uuid rules and the ground-tile convention; `TemplateValidationTest` checks every folder. |
| `source/tests/` | `ECS3DTests` (GoogleTest) and `managed/` `ECS3DManagedTests` (xUnit), both registered with CTest. |
| `tools/shapes/` | Python generator for the committed default models and textures in `source/apps/server/defaultAssets/`. |
| `docs/guides/` | User-facing guides. Keep their claims true to the code when editor or scripting behavior changes. |
| `docs/architecture/` | Design notes, one topic per file; see the [documentation map](#documentation-map). |
| `.github/workflows/` | `cmake-multi-platform.yml` builds Release and Debug on Windows (MSVC), Linux (gcc, clang) and macOS (clang) and runs the `check` target, plus sanitizer and coverage rows on Linux. |

## Build System

- **CMake 3.29 or newer, C++23.** Executables land in `bin/` when ECS3D is the top-level project.
- **Dependencies** are declared with `FetchContent` in `source/libs/CMakeLists.txt` (nlohmann/json, glm, stduuid,
  nativefiledialog-extended, VulkanEngine); test-only ones go in `source/tests/CMakeLists.txt`. **glm is declared
  first on purpose:** FetchContent is first-wins and VulkanEngine also fetches glm, so one pinned copy serves both.
  If VulkanEngine bumps glm, bump the tag here to match.
- **dotnet 10** builds the managed assemblies through `ecs3d_add_managed_assembly()`, which publishes them beside
  the executables: `bin/scripts/ScriptBridge`, `bin/net/Transport` and `bin/scripts/UserScripts`.
- **The runtime current working directory is the executable's directory.** Asset, managed-assembly and
  `nethost.dll` paths resolve relative to it, and a project's asset paths resolve against its root first.
- **Source lists are explicit** in each library's `CMakeLists.txt`. **Add every new file to its library's list.**
- **Precompiled headers** are set up per target at the end of `source/CMakeLists.txt`; a new multi-file target adds
  itself there. **Still include what you use** - the precompiled header is not a substitute.
- **Dependency direction (must hold unless a deliberate change updates this section):** `log` -> nothing.
  `protocol` -> nothing. `profile` -> nothing. `settings` -> log (+ json). `data` -> protocol + log
  (+ json/glm/uuid). `sim` -> data. `render` -> data + VulkanEngine. `editor` -> data + render + settings + nfd + log.
  `net`/`scripting` -> data + clrHost + log. `clrHost` -> log. Apps compose these. **Do not add a Vulkan or ImGui include
  to `data`**, and do not make the server link render or editor: those invariants keep the server headless.
- Detail (managed publishing, precompiled headers, module scanning, project layout, tests):
  [build.md](docs/architecture/build.md), [project-layout.md](docs/architecture/project-layout.md),
  [testing.md](docs/architecture/testing.md).

## Architecture Overview

**The data/systems split.** `ECS3DData` holds only *state*: component fields plus `serialize`/`loadFromJSON`
(files) and `pack`/`unpack` (wire). Behavior lives in *systems* that operate on that data from outside:
`PhysicsSystem`/`CollisionSystem` (sim), `RenderSystem` (render), `ScriptSystem` (scripting) and the inspector
handlers (editor). A component does not reach back into a manager or renderer; systems iterate
`ObjectManager::getAllObjects()` and pull the components they care about.
`ComponentRegistry` maps type names to factories, so no layer names concrete component types across the boundary.

**Client / server.** `ServerApp` is authoritative and headless, and the only app that links `ECS3DSim` and
`ECS3DScripting`. It runs a fixed-timestep loop (scripts, then physics, then collisions) and streams state out.
`ClientApp` renders and sends input. `EditorApp` is a client plus the ImGui tooling, and its authoritative scene
lives on a spawned `--edit` server, so **edits are commands sent to the server, not local mutations**. A loaded
scene starts stopped until the editor presses play, and **stopping a scene discards every runtime change**: `stop()`
rebuilds the object tree from the snapshot `start()` took, uuids preserved.

**Replication.** Structure travels as a full `ProjectPacker` snapshot, per-tick motion as a binary `stateDelta`,
and editor edits as typed commands that only a connection the transport authorized as `Role::editor` may send.
Undo sends a new reverse edit through that same path rather than rewinding locally.

## Documentation map

Each design note is the single home for its topic. A folder's `AGENTS.md` loads with the first file read there.

| Topic | Read before you change | Note |
|-------|------------------------|------|
| Replication | a wire payload, a `sceneEdit` op, an asset mutation, snapshot contents | [replication.md](docs/architecture/replication.md) |
| Prefabs | prefab bodies, instantiation, prefab editing | [prefabs.md](docs/architecture/prefabs.md) |
| Transport and CLR | `Protocol.h` framing, size limits, the C# backends, handshake roles, shutdown | [transport.md](docs/architecture/transport.md) |
| Scripting runtime | `ScriptSystem`, script lifecycle, hot reload, scene scripts, script events | [scripting.md](docs/architecture/scripting.md) |
| Script bindings | a binding provider or `BindingCoverage.h` | [script-bindings.md](docs/architecture/script-bindings.md) |
| Physics and collision | response, friction, resting, the narrow phase, inertia, hulls | [physics.md](docs/architecture/physics.md) |
| Editor library | inspector, selection, asset panel, settings, save/load | [editor.md](docs/architecture/editor.md) |
| Undo and redo | an edit kind, `EditHistory`, an editor mutation path | [undo-redo.md](docs/architecture/undo-redo.md) |
| Viewport gizmos | the transform gizmo, picking, light gizmos, scene drops | [viewport-gizmos.md](docs/architecture/viewport-gizmos.md) |
| Camera and input | `Camera`, player input forwarding, possession | [camera-and-input.md](docs/architecture/camera-and-input.md) |
| Server log forwarding | `RemoteLogSink`, `serverLog`, the Console panel | [logging.md](docs/architecture/logging.md) |
| Profiling | a `profile::Lane`, the scope macro or the history | [profiling.md](docs/architecture/profiling.md) |
| Log, settings, data, render | those libraries' components and helpers | [library-reference.md](docs/architecture/library-reference.md) |
| Build | managed publishing, precompiled headers, module scanning | [build.md](docs/architecture/build.md) |
| Project layout | asset path resolution, project roots, templates, save and import | [project-layout.md](docs/architecture/project-layout.md) |
| Tests | fixtures, the sources compiled into the suite, the managed test project | [testing.md](docs/architecture/testing.md) |
| Applications | the client/server loop, an app's flags, startup or logging | [apps.md](docs/architecture/apps.md) |

Folders with their own `AGENTS.md`: `source/libs/{data,sim,render,editor,net,profile,scripting}`, `source/apps/{server,editor,launcher}`,
`source/tests` and `source/tests/managed`.

## Development Principles

- **Namespaces are the exception**, used where a name is generic enough to collide: network code in `namespace
  net`, the GJK/EPA entry points in `namespace collisions`. Default to no namespace.
- **Headers:** `#ifndef NAME_H` include guards; forward-declare across libraries and include in the `.cpp`.
- **Naming:** `PascalCase` types and files, `camelCase` methods and locals, `m_` member prefix. `[[nodiscard]]` on
  getters and queries; prefer `const` accessors.
- **Ownership:** subsystems are shared through `std::shared_ptr`, passed as `const std::shared_ptr<T>&`.
  `vke::raii` owns Vulkan handles - do not destroy them manually.
- **The serialize/loadFromJSON boundary is the contract.** Replication, save/load and the registry all ride on it.
  Thread a new component field through it, and no layer learns the concrete type.
- **A user preference is not project data.** Project state replicates to every client, so preferences live in
  `ECS3DSettings` (a JSON file in the per-user data directory), not in a scene, a prefab or a snapshot.
- **Respect the layer boundaries.** A new component splits into fields (`data`), physics (`sim`), rendering
  (`render`), bindings (`scripting`) and an inspector widget (`editor`), and registers in `registerDataComponents()`.
- **Comments:** write them sparingly. Prefer self-documenting code (clear names, small functions) over a comment.
  Comment only what the code cannot say for itself - the *why* behind a non-obvious algorithm, design decision,
  workaround or caveat - in one or two lines. Do not narrate implementation history ("was X", "moved from Y",
  "Phase N", "temporary") or point at external plans. **Comments are ASCII (plain 7-bit text) only:** use `-`, `->`
  and `<->` instead of em dashes, arrows or other Unicode.

## Applications

- **ECS3DServer** (`apps/server`) - the headless authoritative simulation. Links Data, Sim, Scripting, Net, ClrHost
  and Log. Generates the built-in default project when started without `--project`.
- **ECS3DClient** (`apps/client`) - the lightweight view. Links Data, Render, Net, ClrHost and Log. Spawns a local
  server for singleplayer; `--host` connects to an existing one.
- **ECS3DEditor** (`apps/editor`) - a client plus the ImGui tooling. Links Data, Render, EditorLib, Net, ClrHost
  and Log. Spawns its own `--edit` server with a generated token unless `--host` names one.
- **ECS3DLauncher** (`apps/launcher`) - the standalone C# project hub, built by `dotnet publish` through CMake.
- Every app logs through `Log` to the console and to `<user data dir>/logs/<app>.log`. The command-line flags,
  the edit token and the spawned servers' logs are in [apps.md](docs/architecture/apps.md).

## Keeping the docs current

Update documentation in the same change as the behavior it describes. Put each fact in one place:

- **A project-wide safeguard or convention** goes here. Keep this file within its budget; move detail out rather
  than growing it.
- **A rule that matters only when editing one folder** goes in that folder's `AGENTS.md`. Add one when a new folder
  has rules a change could break.
- **Design that spans folders** goes in `docs/architecture/<topic>.md`, with a row in the documentation map.
- Link to the home of a fact rather than restating it, and point at code, a test or a build-enforced table where one
  already says it.
- Describe the code as it is now, with no history narration, and delete what stops being true.
