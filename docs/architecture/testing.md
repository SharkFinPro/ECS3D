# Tests

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

## Fixtures

Test fixtures live in `source/tests/TestScene.h` (namespace `fixtures`). `fixtures::Scene`
opens an `ObjectManager` over a `ComponentRegistry`, registering the data components in it unless the
`Components::none` constructor argument says otherwise - the setup every suite used to repeat - and the
free functions beside it add objects, colliders and rigid bodies, and compare `glm::vec3` with a
tolerance and a trace. Derive from `fixtures::Scene` to hang extra members off a scene. Only
`addObject`/`addChildObject` are reachable unqualified, by ADL (argument-dependent lookup) through their `Scene` argument;
everything else takes arguments that do not name `fixtures` (or, for `makeScene`, none at all), so it
needs `fixtures::` or a using-declaration. **Build a scene through these rather than re-deriving the scaffolding in a new suite.**

## Native suite

Tests (`source/tests/`) build as `ECS3DTests`, linking `ECS3DData`, `ECS3DSim`, `ECS3DSettings` and
`ECS3DNetProtocol` — not the renderer, the editor or `ECS3DNet` (bar the few sources compiled in directly, below) — so the suite stays runnable without a
window, GPU or server. `net/MessageQueue.cpp` (and the CLR-free `net/CommandLineSplit.cpp`, `net/ClientConnectionState.cpp` and `net/EditorConnections.cpp`) are compiled straight into the target rather than linked,
because it is the one piece of `ECS3DNet` with no CLR dependency; see the comment in the test
`CMakeLists.txt` before adding more. The server's `DefaultProject.cpp`, `DefaultShapes.cpp`, `DefaultWelcome.cpp` and `RestPose.cpp` are compiled in the same
way, as is
`scripting/ScriptSystem.cpp` (it depends only on data, log, json and the `ScriptRuntime` interface), as is
`editor/Gizmo.cpp` and `render/Billboard.cpp` (each depends only on glm), the header-only `render/SharedAssetLoads.h` (depends only on `ECS3DLog`), the header-only `render/CapsuleGizmo.h` (depends only on glm), the header-only `render/HullGizmoMesh.h` (depends on glm and the hull type) and `editor/TransientObject.cpp` (depends only on `ECS3DData` and
json), so the suite gets them without pulling in the rest of `ECS3DEditorLib` and the ImGui/Vulkan it
carries), `editor/AssetReferences.cpp` (depends only on ECS3DData and json), `editor/components/ConvexHullRebuild.cpp` (depends only on ECS3DData, glm and log); header-only editor headers such as `ObjectTreeOrder.h`, `KeyHoldRepeat.h`, `ProjectLoadTracker.h`, `RecentProjectRecording.h`, `ScriptFieldAsset.h` and `ScriptFieldLabel.h` are reached
through the `libs/editor` include path alone. The header-only `apps/server/ServerPolicy.h`
(mutation authorization, scene-control plans, token generation check) is covered the same way, by including it, and the
header-only `libs/net/EditToken.h` is reached through the `libs/net` include path. `PresetsTest` reads `CMakePresets.json` from the repo root via the `ECS3D_SOURCE_DIR` compile definition. It builds into `<build-dir>/tests`, not `bin/`. GoogleTest is fetched in
`tests/CMakeLists.txt` rather than with the shared deps, and the directory is gated on
`PROJECT_IS_TOP_LEVEL` and `BUILD_TESTING` together — `BUILD_TESTING` is a cache variable a parent project may
already have set, so the top-level check is what actually keeps an embedded ECS3D from fetching
GoogleTest. `gtest_discover_tests` registers every case with CTest, and the `check` target builds the
suite and runs it: `cmake --build <build-dir> --target check`. That target is what CI runs too, so a
defect in it is caught rather than shipped; it passes `--no-tests=error`, since ctest exits 0 on an
empty test set and would otherwise report green for a suite that registered nothing. It also passes
`--parallel` (one test per logical CPU), and `ECS3DManagedTests` has a high test cost so it starts first.
Under the `ecs3d-coverage` preset (`ECS3D_COVERAGE`) the same target also leaves the managed suite's
cobertura output in `<build-dir>/coverage/managed`; the native side is read with gcovr.
`render/ThumbnailImage.cpp` is also compiled straight into the suite; it needs only stb_image_write.
VulkanEngine fetches stb, so the suite reads its include directory from the global `stb` FetchContent property.

## Managed suite

Managed tests (`source/tests/managed/`) build as `ECS3DManagedTests`, an xUnit project registered
as a single CTest test through `add_test` in `tests/CMakeLists.txt` (guarded on `DOTNET_EXE`) rather
than a native executable. The launcher's `source/Services` (plain C#, no Avalonia) are compiled into it by
link, and so are its view models, models and `IconKeyConverter`, over the Avalonia and
CommunityToolkit.Mvvm packages; the window, views and `DesignData` stay out. It covers pure, testable
logic via `ProjectReference`s to
`Transport/ECS3DNetTransport.csproj` (wire framing/handshake) and `ScriptBridge/ScriptBridge.csproj`
(the `[ExposeToEditor]` field reflection/JSON and value-conversion logic behind `getExposedFields`/
`getField*`/`setField*` — `Bridge.BuildExposedFieldsJson`/`FindExposedField`/`ReadExposedField`/
`TryConvertFieldValue` (including its string rules)/`StringFieldToCoTaskMem`/`MapTypeName`/`Key`, plus the by-name script lookup `TryFindScript`/`ScriptHandle`, and the fault gate and init/reload sweep through `RunGuarded`/`Initialize`/`Reload`, which `ResetForTests` returns to a fresh state). The component wrappers (`World`, `Transform`, `RigidBody`, `Camera`, `Collider`, `ModelRenderer`,
`LightRenderer`, `PlayerController`, `InputUtils`/`PlayerInput`, `ScriptBase`) are covered too:
`FakeNatives.Install()` points every `NativeBindings.*` table at `[UnmanagedCallersOnly]` fakes that record
each call and answer from canned values (a native `bool` is a `byte` there, since the test assembly keeps runtime
marshalling on), and restores the tables when disposed; those suites share the serial `BridgeInstances`
collection.
`ShippedScriptsCompileTests` compiles every shipped script through `ScriptCompiler`, the compile step `Bridge`
uses at runtime. It finds the source tree through an `ECS3DSourceRoot` assembly attribute the csproj sets.
`TcpBackendLoopbackTests` also drives a live `TcpBackend` over loopback sockets: `TransportRecorder`
takes the addresses of `Transport`'s `[UnmanagedCallersOnly]` `set*Callback` exports as function pointers to
register its own callbacks (and unregisters them afterward), and records what the backends deliver, so
those tests share one non-parallel xUnit collection (`Transport`) and shorten `TcpBackend`'s internal
`SendTimeoutMs`/`BodyReadTimeoutMs`/`HandshakeTimeoutMs` fields and set `AcceptedSendBufferBytes` rather
than waiting out production values.
`WebSocketBackendLoopbackTests` drives the WebSocket backend the same way: its `SendTimeoutMs` is also an internal
field, and an internal `ClientReceiveLoopExiting` hook lets a test reconnect inside the window between the
client receive loop stopping and releasing its connection. Both loopback backends have an internal
`ServerConnectionExiting` hook, which lets a test hold a connection thread just before its disconnect is
delivered. `TransportExportsTests` calls `Transport`'s exports through function pointers.
The test project also compiles `UserScripts/` in (a `Compile` link in `ECS3DManagedTests.csproj`), so
`PlayerScript` and the starter scripts are tested through `FakeNatives` like the wrappers; a fake can
answer per uuid, name or key code by setting `"<fake key>:<argument>"`.
It also links `source/templates/coin-rush/scripts`, so the sample's `ScorePillar` is tested through
`FakeNatives` too.
`BridgeEntryPointTests` calls `ScriptBridge`'s `[UnmanagedCallersOnly]` entry points through function
pointers taken with `&`, with string arguments marshalled as native code does, as `TransportExportsTests`
does for `Transport`'s exports. Logic outside the entry points is tested as pure code over plain objects
or through a fake binding table. Private product logic is exposed to this
project through `internal` + `InternalsVisibleTo` (declared in `source/libs/net/Transport/AssemblyInfo.cs` and
`source/libs/scripting/ScriptBridge/AssemblyInfo.cs`), not reflection. Its `Directory.Build.props` redirects `obj`/`bin`
under `$(BuildRoot)obj/tests/` and `$(BuildRoot)bin/tests/` — one level deeper than `Transport`'s own
`$(BuildRoot)obj/` — so referencing `ECS3DNetTransport` by `ProjectReference` (which inherits the
`BuildRoot` global property `dotnet test` is invoked with) keeps the two projects' generated `obj`
output in separate directories — the CS0579 trap below. `ScriptBridge`'s own `Directory.Build.props`
additionally checks `ecs3d_add_managed_assembly`'s `DOTNET_BASE_INTERMEDIATE_OUTPUT_PATH` env var
itself (not the `.csproj` body, which is imported too late for `BaseIntermediateOutputPath` to reliably
take a value set there — the same `MSB3539` timing this file's own `Directory.Build.props` notes): when
that env var is set (the CMake publish), it wins unchanged; otherwise (a `ProjectReference` build under
someone else's `BuildRoot`, as `ECS3DManagedTests` now makes) the fallback nests under a `ScriptBridge/`
subfolder (`$(BuildRoot)obj/ScriptBridge/`) rather than the literal `$(BuildRoot)obj/` `Transport` uses,
so referencing both from this project under one shared `BuildRoot` does not point their generated `obj`
output at the same directory — the same trap between the two referenced assemblies instead of between
this project and one of them. The
CTest command is `dotnet test <csproj> -c Release -p:BuildRoot=...`, so building the project happens
only when ctest runs it, not as a step of a plain `cmake --build`: add a new managed test by adding a
`.cs` file under `source/tests/managed/` (globbed automatically, unlike the native suite's explicit
source list) and running it through `check`, the same way as every other test here. On a machine with
a cold NuGet cache, that first `check` run restores `xunit`, `xunit.runner.visualstudio`, the
test-SDK package, `Avalonia`, `CommunityToolkit.Mvvm`, and (via `ScriptBridge`)
`Microsoft.CodeAnalysis.CSharp` (the first five are listed in `ECS3DManagedTests.csproj`), which needs
network access.
