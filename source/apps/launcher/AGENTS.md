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
  **CommunityToolkit.Mvvm** (`[ObservableProperty]`, `[RelayCommand]`, `ObservableObject`).
- **Status: UI shell only.** Everything is presentation right now — the project/template/sample lists are
  hardcoded sample data (see `ProjectsViewModel`, `DesignData`) and commands only drive navigation. There
  is **no engine backend wired in yet**: no real project loading, no process launching, no persistence.
- Ships as a single-file `ECS3DLauncher.exe` beside the C++ executables in `bin/`. It is
  **framework-dependent** (shares the machine's installed .NET 10) and does **not** boot the CLR the way
  the engine apps do — it is just an ordinary .NET desktop process.

## Layout

All source lives under `source/` (mirrored by the `avares://ECS3DLauncher/source/...` resource authority).

| Path | Responsibility |
|------|----------------|
| `source/Program.cs` | Avalonia entry point (`BuildAvaloniaApp`). Minimal — no app logic. |
| `source/App.axaml{,.cs}` | Application composition: merges `Theme.axaml` + `Icons.axaml` resources and `AppStyles.axaml`; sets the dark `FluentTheme`; creates the single `MainWindow` with its `MainWindowViewModel`. Attaches Avalonia DevTools (F12) in DEBUG only. |
| `source/Shell/` | The window frame and navigation. `MainWindow.axaml{,.cs}` (frameless window — manual edge-resize/move/maximize in code-behind), `MainWindowViewModel` (sidebar nav + which tab is active + derived title/subtitle), `NavItemViewModel`. |
| `source/Tabs/` | One folder per library section: `Projects`, `Templates`, `Samples`, `Learn`, `Settings`. Each has a `*View.axaml{,.cs}` + `*ViewModel.cs`. The shell owns navigation; a tab only asks the shell to switch (e.g. Projects → Templates). |
| `source/Shared/` | Cross-cutting pieces: `Models/` (`Project`, `ProjectTemplate` — plain records), `Resources/` (`Theme.axaml` palette+fonts, `Icons.axaml` geometries), `Styles/AppStyles.axaml` (style selectors), `Controls/` (`EmptyState`), `Converters/` (`IconKeyConverter`), `DesignData.cs`. |
| `source/Assets/` | Bundled `AvaloniaResource`s: `Fonts/` (Geist, GeistMono) and `scene_render.png`. |

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
  source-generated `[ObservableProperty]`/`[RelayCommand]`). `AvaloniaUseCompiledBindingsByDefault` is on —
  give bindings an `x:DataType` so they compile. `Nullable` is enabled.
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
- **Code-loaded brushes:** where a bound brush won't inherit the item `DataContext` (e.g. gradient stops),
  build the brush in the model instead of XAML — see `Project.GlowBrush` for the pattern and the why.

## AI Agent Guidelines

- Read this file before changing the launcher. You should **not** need the root `AGENTS.md` or any C++
  engine knowledge for launcher work.
- This is a **UI shell with placeholder data.** When asked to "load projects" / "open the editor" / etc.,
  note that no backend exists yet — confirm whether the task is to build real functionality (new service +
  wiring) or extend the mock UI, rather than assuming a backend is present.
- Keep the MVVM split clean: no app logic in code-behind, no view construction in view models.
- Add UI dependencies via `PackageReference` in `ECS3DLauncher.csproj`; keep Debug-only tooling under the
  `Configuration == Debug` item group so it never ships.
- Prefer extending existing tabs/resources over introducing parallel navigation or theming systems.
- Update this document when the launcher's structure or conventions meaningfully change — especially once
  a real backend is wired in.
