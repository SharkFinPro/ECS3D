# AGENTS.md — ECS3D Launcher

> **Living document.** Keep this current. When architecture, conventions, workflows, dependencies, or
> structure change, update this file in the same change. Keep it concise. Read this before making changes.
>
> **This is a standalone project.** The ECS3D Launcher is a self-contained C# / Avalonia desktop app.
> It shares no code, build step, or runtime with the C++ engine that lives in the rest of the repo —
> you do **not** need to understand the engine to work on it. (For how it fits into the whole repo, see
> the root `AGENTS.md`; you rarely need it here.)

## Project Overview

- **ECS3DLauncher** is the project-management front door for the ECS3D engine: a dark-themed desktop GUI
  for browsing recent projects, templates, samples, and learning material — the "hub" you'd open before
  launching the editor.
- Built with **Avalonia 12** (cross-platform XAML UI) on **.NET 10**, using the **MVVM** pattern via
  **CommunityToolkit.Mvvm** (`ObservableObject`, `SetProperty`, `RelayCommand`).
- **Status: the shell locates and launches the engine.** At startup it finds the engine executables, shows the
  engine-not-found banner when one is missing, opens projects and the built-in Welcome project in the editor,
  and toasts a failed start or an early exit. The launcher loads `launcher-settings.json` at startup and uses it:
  editor launches take their console flags from it, the engine folder override feeds `EngineLocator`, and the
  Projects grid/list choice starts from it. The Settings tab saves each change at once (text boxes when they lose focus or on Enter), and a changed engine folder
  re-locates the engine and refreshes the banner and the Explore/Open actions.
  The Projects tab lists the stored recent projects (pinned first, then newest). A card opens, plays, pins,
  removes (from the list only) and shows the project in the file manager. While the list is empty, three first-run
  cards replace it and the grid/list toggle is hidden. The list reloads when the window regains focus; a file that
  cannot be read is never overwritten. A project whose folder or project file is gone keeps its card, shows
  "Folder not found" and offers Locate (pick the new folder) and Remove instead of Open. The check runs on every
  refresh. Open existing... in the Projects header records a project folder (via its `project.ecs3d`) or a legacy
  `.json` file and opens it. While an editor the launcher started is running, its card shows "In editor"
  (`RunningEditors`, counted per project; the launcher never kills an editor). The Templates tab is real: each
  `template` entry of `TemplateCatalog` is a card, gated by `minEngineVersion` and filtered by the header search;
  clicking one does nothing until the New Project dialog lands. The Samples tab is
  real: `TemplateCatalog` reads `<engine>/templates`, each `sample` becomes a card, the header search filters them,
  and Play runs `ECS3DClient --project <shipped folder>`. Host and Open a copy are not built yet.
  The Learn tab is real: six guide cards from `LearnCatalog` that open on GitHub in the browser, plus a "What's
  in the box" banner.
  "Close the launcher after opening a project" is saved but not honored yet.
- Ships as a single-file `ECS3DLauncher.exe` beside the C++ executables in `bin/`. It is
  **framework-dependent** (shares the machine's installed .NET 10) and does **not** boot the CLR the way
  the engine apps do — it is just an ordinary .NET desktop process.

## Layout

All source lives under `source/` (mirrored by the `avares://ECS3DLauncher/source/...` resource authority).

| Path | Responsibility |
|------|----------------|
| `source/Program.cs` | Avalonia entry point (`BuildAvaloniaApp`). Minimal — no app logic. |
| `source/App.axaml{,.cs}` | Application composition: merges `Theme.axaml` + `Icons.axaml` resources and `AppStyles.axaml`; sets the dark `FluentTheme`; creates the single `MainWindow` with its `MainWindowViewModel`. Attaches Avalonia DevTools (F12) in DEBUG only. |
| `source/Shell/` | The window frame and navigation. `MainWindow.axaml{,.cs}` (frameless window — manual edge-resize/move/maximize in code-behind), `MainWindowViewModel` (sidebar nav, the active tab and its header, the engine banner, the toast, the Explore/Open commands), `NavItemViewModel`. |
| `source/Tabs/` | One folder per library section: `Projects`, `Templates`, `Samples`, `Learn`, `Settings`. Each has a `*View.axaml{,.cs}` + `*ViewModel.cs`. The shell owns navigation; a tab only asks the shell to switch (e.g. Projects → Templates). |
| `source/Services/` | UI-free services behind interfaces (`EngineLocator`, `EngineLauncher`, `EarlyExitWatcher`, `UserDataDirectory`, `AppVersion`, `IFileSystem`, `ILauncherLog`/`FileLauncherLog`, `LauncherSettings`/`LauncherSettingsStore`, `SettingsInput`, `EnginePorts`, `HostSession`, `IPortProbe`/`TcpPortProbe`, `LauncherTabs`, `EngineActions`, `EarlyExitNotice`, `LaunchCoordinator`, `VersionLabel`, `SearchMatch`, `INetworkInfo`/`SystemNetworkInfo`/`LanAddresses`, `JoinTarget`, `RecentProjectsStore` (the launcher side of `recent-projects.json`), `RecentProjectsView` (order, search, missing check and Edited text of that list), `ProjectPathKind` (classifies an Open existing... choice: folder, `project.ecs3d` or legacy `.json`), `RunningEditors` (editors started per project, for the In editor badge), `TemplateCatalog`/`TemplateManifest`, `LearnCatalog` (the Learn tab's guides and shipped-content counts), `EngineVersion`, `ProjectNameRules`, `SettingsEditor` - the Settings tab's decisions: apply, validate, re-locate and save). No Avalonia types. |
| `source/Shared/` | Cross-cutting pieces: `Models/` (`Project`, `SampleCard`, `TemplateCard` — plain records), `Resources/` (`Theme.axaml` palette+fonts, `Icons.axaml` geometries), `Styles/AppStyles.axaml` (style selectors), `Controls/` (`EmptyState`, `CardMenu` - opens a card's context menu from its ... button), `Converters/` (`IconKeyConverter`), `DesignData.cs`. |
| `source/Assets/` | Bundled `AvaloniaResource`s: `Fonts/` (Geist, GeistMono) and `scene_render.png`. |

**Shared files:** `recent-projects.json` is also written by the editor; its schema is in
`source/libs/settings/RecentProjects.md` (repository root).
`launcher-settings.json` is the launcher's own (`LauncherSettingsStore`, schema in `LauncherSettings.md`). It is
written atomically, keeps unknown fields, and falls back to defaults with a line in `logs/launcher.log` when unreadable.

## Build & Run

- **Standalone (the normal way to develop this):** `dotnet run` from this directory, or open the folder in
  Rider/VS. The SDK uses default `obj/` and `bin/` locations (both git-ignored).
- **As part of the engine (CMake):** the root build treats this as an `add_executable` target for CLion's
  sake — it compiles a trivial C stub to `bin/ECS3DLauncher.exe`, then a `POST_BUILD` step overwrites it
  in place with the real single-file `dotnet publish` output. See `CMakeLists.txt` (heavily commented).
  Under CMake, `obj/bin` intermediates are redirected into the CMake tree via `Directory.Build.props`
  (`LauncherObjDir`/`LauncherBinDir`) so the source tree stays clean.
- **Config split matters:** `BaseIntermediateOutputPath`/`BaseOutputPath` live in `Directory.Build.props`
  (not the `.csproj`) because they must be set before the SDK imports its common props and be visible to
  `restore`. Don't move them into the `.csproj`.
- **Release vs Debug:** CMake always publishes **Release**. Debug-only dev tooling (HotAvalonia XAML
  hot-reload, `AvaloniaUI.DiagnosticsSupport` DevTools) is gated to `Debug` in the `.csproj` and must
  never ship. The `AssemblyName` is `ECS3DLauncher` (also the `avares://` authority) — keep it in sync
  with `Theme.axaml`/`ProjectsViewModel` resource URIs if renamed.

## Conventions

- **MVVM, strictly.** Views are XAML; state and commands live in view models (`ObservableObject` +
  hand-written `SetProperty` properties and `RelayCommand` fields built in the constructor - no source generators, so no `partial` classes). `AvaloniaUseCompiledBindingsByDefault` is on —
  give bindings an `x:DataType` so they compile. `Nullable` is enabled.
- **Services stay free of Avalonia and CommunityToolkit references.** This is a convention, so they remain
  plain, testable logic; a type that needs those packages belongs in `Tabs` or `Shared`. `ECS3DManagedTests`
  links `source/Services/**`, the view models (`Shell/MainWindowViewModel.cs`, `Shell/NavItemViewModel.cs`,
  `Tabs/**/*ViewModel.cs`), `Shared/Models`, `Shared/Converters` and `Shared/Controls/CardMenu.cs`. A new shell
  view model, or one not named `*ViewModel.cs`, must be added to that link list. Anything that touches the
  filesystem, processes or the environment goes behind an interface or an injected function.
- **Navigation is centralized.** `MainWindowViewModel.ActiveNav` (a string key) selects `CurrentTab`; the
  window's `ContentControl` maps the active view model to its view by type. Tabs never navigate directly —
  they call back into the shell (see `ProjectsViewModel`'s `openTemplates`).
- **Theming lives in `Shared/Resources` + `Shared/Styles`.** Colors, brushes, fonts → `Theme.axaml`;
  icon geometries → `Icons.axaml`; control/style selectors → `AppStyles.axaml`. Don't hardcode colors in
  views — reference the palette resources. Icons are chosen by string key resolved through
  `IconKeyConverter` (uses `TryGetResource` so it traverses merged dictionaries).
- **Design-time preview:** reference `Shared/DesignData` via `d:DataContext` so views render real content
  in the previewer. `DesignData` is design-only and stripped at runtime.
- **Frameless window quirks:** `MainWindow` is `WindowDecorations="None"`, so edge-resize, move, and
  maximize/restore are reimplemented in `MainWindow.axaml.cs`. Keep window-chrome logic there; keep app
  logic in view models.
- **Code-behind loads XAML itself.** Views call `AvaloniaXamlLoader.Load(this)` and nothing in the
  launcher is `partial` (Sonar S2333). Avalonia's name generator is filtered to an empty namespace in the
  `.csproj`, so a view that needs a named control looks it up with `FindControl<T>` right after the load, as
  `MainWindow` does. View models use `SetProperty` and `RelayCommand` by hand for the same reason.
- **Code-loaded brushes:** where a bound brush won't inherit the item `DataContext` (e.g. gradient stops),
  build the brush in the model instead of XAML — see `Project.GlowBrush` for the pattern and the why.

## AI Agent Guidelines

- Read this file before changing the launcher. You should **not** need the root `AGENTS.md` or any C++
  engine knowledge for launcher work.
- Engine location and launching, settings, the recent-projects store and the template catalog (Samples tab) are
  real; project creation and the hosting UI are not yet. When asked to "load projects" and the like, confirm
  whether the task is to build the missing service and its wiring or to extend the mock UI.
- Keep the MVVM split clean: no app logic in code-behind, no view construction in view models.
- Add UI dependencies via `PackageReference` in `ECS3DLauncher.csproj`; keep Debug-only tooling under the
  `Configuration == Debug` item group so it never ships. `ECS3DManagedTests.csproj` pins the same Avalonia and
  CommunityToolkit.Mvvm versions, so bump both together.
- Prefer extending existing tabs/resources over introducing parallel navigation or theming systems.
- Update this document when the launcher's structure or conventions meaningfully change — especially once
  a real backend is wired in.
