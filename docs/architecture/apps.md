# Applications and the client/server loop

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

## Client and server

`ServerApp` is authoritative and headless — it is the **only** application that links
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
  only when `--edit` is set — see [logging.md](logging.md).
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

## App icons

The editor and client icons are the multi-size `.ico` files in `source/apps/icons/`. On Windows,
`ecs3d_add_app_icon()` (`source/apps/AppIcon.cmake`) generates a one-line resource script that embeds the file as
`GLFW_ICON`. GLFW's Win32 backend loads that name for its window class, and Explorer shows the first icon resource,
so one resource covers the executable file, the title bar and the taskbar, with no runtime code. The server is
headless and has no icon.

The cache variables `ECS3D_EDITOR_ICON` and `ECS3D_CLIENT_ICON` default to the shipped files. An application built on
the engine sets them to its own `.ico` files. The resource script names the icon as an object dependency, so editing
the file rebuilds the resource on generators that do not scan it. `AppIconTest` checks that both shipped files are
well-formed icons with the standard sizes.

The helper does nothing off Windows. Linux and macOS window icons are not done yet: X11 needs `glfwSetWindowIcon`
with decoded pixels, Wayland ignores window icons, and macOS takes its icon from an app bundle.
