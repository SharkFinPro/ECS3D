# AGENTS.md - ECS3DManagedTests

Agents load this file when they read a file in `source/tests/managed/`. The full notes are in the Managed suite
section of [docs/architecture/testing.md](../../../docs/architecture/testing.md).

- The project runs only through ctest (the `check` target), which passes the `BuildRoot` isolation. Running
  `dotnet build`, `dotnet test` or `dotnet publish` on it, or on a project it references, skips that isolation and
  breaks the next CMake build with error CS0579.
- New `.cs` files here are picked up by a glob, so there is no source list to edit.
- Product logic reaches the tests through `internal` plus `InternalsVisibleTo`, not reflection.
- `FakeNatives.Install()` points every native binding table at recording fakes. Suites that use it share the serial
  `BridgeInstances` collection, the loopback transport suites share the `Transport` collection, and the headless
  Avalonia tests share the `AvaloniaHeadless` collection. A launcher view under test is linked into
  `ECS3DManagedTests.csproj` (code-behind plus an `AvaloniaXaml` item) and gets the theme resources it needs from
  the test; `LauncherCardMenuTests.ShowInWindow` hosts it in a themeless window that still joins the visual tree.
- A cold NuGet cache needs network access on the first run.
