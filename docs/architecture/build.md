# Build

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

## Top-level configuration

Top-level config: C++23, `bin/` output when top-level, `compile_commands.json`, `include(CTest)`, the
`ECS3D_SANITIZE`, `ECS3D_COVERAGE` and `ECS3D_PROFILE` options, `CMAKE_CXX_SCAN_FOR_MODULES` off by default,
MSVC (the Microsoft Visual C++ compiler) export-all-symbols. Then `add_subdirectory(source)`.

Module scanning is off (`CMAKE_CXX_SCAN_FOR_MODULES`, root `CMakeLists.txt`). Nothing uses C++20
modules, and on MSVC the scan cost about an eighth of a clean build.

## Managed assemblies

dotnet 10 (the C# runtime and SDK) is required for the managed assemblies. `ecs3d_add_managed_assembly()` (in
`clrHost/cmake/ECS3DManaged.cmake`) `dotnet publish`es a C# class lib next to the executables and
writes its `runtimeconfig.json`; `ecs3d_deploy_clr_runtime()` copies `nethost.dll` beside each exe.
`FindDotnet.cmake` warms up the SDK's one-time first-run configuration once at configure time (a
single-threaded step that runs before any target's publish is scheduled), so no build-time `dotnet
publish` races through it; the two engine managed assembly targets additionally depend on each other
so they publish one at a time, while the launcher (`source/apps/launcher`) publishes independently.

Managed assemblies deployed at build time: `ScriptBridge` → `bin/scripts/ScriptBridge`,
`ECS3DNetTransport` → `bin/net/Transport`, user scripts → `bin/scripts/UserScripts`. All apps boot the
CLR (Common Language Runtime) from the same runtime; the server loads `ScriptBridge` on top.

## Precompiled headers

Precompiled headers (PCH) are set up in one block at the end of `source/CMakeLists.txt`: each
multi-file ECS3D target gets its own PCH of the most-included standard and third-party headers.
Targets differ in compile definitions (VulkanEngine adds the `GLM_FORCE_*`/`VULKAN_HPP_*` set), so
they do not share one. `windows.h` and GLFW stay out, since four files define macros before including
them. A new multi-file target adds itself to that block. Turning the `ECS3D_PRECOMPILED_HEADERS`
option off disables it. The `ecs3d-includes` preset does, and CI's include check
(`tools/ci/check_includes.py`) compiles every first-party translation unit under it, front end only, so a
file that leans on the PCH for an include fails there. **Still include what you use** - the PCH is not a
substitute.

## CLR host helpers

`ECS3DClrHost` — `ManagedHost` boots CoreCLR and hands out managed statics as native fn ptrs. Owns the CMake
helpers (`cmake/ECS3DManaged.cmake`, `FindDotnet.cmake`, `loadCS.cmake`).

## Templates

Shipped project templates and samples, one folder each (`project.ecs3d`, `assets/`, `scripts/`, `template.json`,
`thumbnail.png`). Its `README.md` documents the manifest, the uuid rules and the ground-tile convention. Copied
to `<runtime output dir>/templates/` at configure time (`apps/server/CMakeLists.txt`, beside `defaultAssets`).
`TemplateValidationTest` checks every folder here.

## Generated default assets

Python generator, using only the standard library, for the generated default models and textures in
`source/apps/server/defaultAssets/`. Its output is committed, so builds do not run it; the README records the
ground-tile convention. `library.py` holds the convex shape library's vertex formulas, a small convex hull and
the flat-shaded models.

## Continuous integration

`cmake-multi-platform.yml` — builds Release **and** Debug on Windows (MSVC), Linux (gcc+clang), macOS (clang)
with the Vulkan SDK, then runs the suite through the `check` target; two more Linux rows (gcc, clang) run the
`ecs3d-sanitize` preset's suite under ASan+UBSan, and one Linux gcc row runs `ecs3d-coverage` and uploads a
gcovr/cobertura `coverage-report` artifact. One Linux clang row configures `ecs3d-includes` and runs
`tools/ci/check_includes.py`, which reads `compile_commands.json` and compiles every first-party translation
unit (libraries, apps and tests; not `_deps` or the launcher) with `-fsyntax-only`, so nothing is built or
linked. A newer push to a pull request cancels that pull request's previous
run.
