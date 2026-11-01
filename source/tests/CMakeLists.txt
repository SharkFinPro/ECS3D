project(ECS3DTests)

include(FetchContent)

# The MessageQueue tests run real producer/consumer threads of their own.
find_package(Threads REQUIRED)

# GoogleTest is the only test-only dependency, so it is declared here rather than alongside the shared
# runtime deps in libs/: a build with BUILD_TESTING off never fetches it.
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.18.0
  GIT_SHALLOW TRUE
)

# GoogleTest links the static MSVC runtime by default; the rest of the tree uses the dynamic one.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(${PROJECT_NAME}
  ColliderPersistenceTest.cpp
  ColliderCacheTest.cpp
  CapsuleColliderTest.cpp
  CoinRushSampleTest.cpp
  CoinRushSamplePhysicsTest.cpp
  CoinRushTestHelpers.h
  CapsuleGizmoTest.cpp
  CapsuleNarrowPhaseTest.cpp
  CapsulePhysicsTest.cpp
  CapsuleRestingTest.cpp
  CapsuleRollingTest.cpp
  CapsuleSceneQueryTest.cpp
  ComponentLookupTest.cpp
  CameraRangeTest.cpp
  CameraSelectionTest.cpp
  ComponentRegistryTest.cpp
  AssetRegistryTest.cpp
  AssetDragDropTest.cpp
  UndoRequestGateTest.cpp
  ConsoleViewTest.cpp
  KeyHoldRepeatTest.cpp
  AssetReferencesTest.cpp
  AssetWireTypeTest.cpp
  UnsavedChangesTest.cpp
  AssetFilterTest.cpp
  ConsolePanelLayoutTest.cpp
  SettingsPanelLayoutTest.cpp
  DashedBoxLayoutTest.cpp
  ObjectTreeOrderTest.cpp
  OutlineColorsTest.cpp
  ComponentEditTest.cpp
  MissedComponentEditLogTest.cpp
  ComponentVariableTest.cpp
  ConvexHullRebuildTest.cpp
  ConvexHullTest.cpp
  ConvexHullValidationTest.cpp
  ConvexMeshColliderTest.cpp
  ConvexMeshManifoldParityTest.cpp
  ConvexMeshNarrowPhaseTest.cpp
  ConvexMeshPhysicsTest.cpp
  ConvexMeshPlacementTest.cpp
  ConvexMeshQueryTest.cpp
  ConvexMultiSelectEditTest.cpp
  HullGizmoMeshTest.cpp
  HullVerificationTest.cpp
  NonFiniteValuesTest.cpp
  FiniteRendererValuesTest.cpp
  EditHistoryRoundTripTest.cpp
  EditHistoryRoundTripSubtreeTest.cpp
  EditHistoryLabelTest.cpp
  EditHistoryRefusalKindsTest.cpp
  EditHistoryRefusalTest.cpp
  EditHistoryStructuralRefusalTest.cpp
  EditHistoryBatchTest.cpp
  EditCommandTest.cpp
  EditHistoryPlayStopTest.cpp
  HistoryScopeTest.cpp
  PlaySessionHistoryTest.cpp
  RecordEditsAssetTest.cpp
  RecordEditsObjectTest.cpp
  RecordEditsReorderTest.cpp
  CollisionMathTest.cpp
  ContactManifoldTest.cpp
  SweepEquivalenceTest.cpp
  NarrowPhaseTest.cpp
  NarrowPhaseRotatedTest.cpp
  NarrowPhaseFaceManifoldTest.cpp
  EpaFlushContactTest.cpp
  SupportFacesTest.cpp
  SceneQueryTest.cpp
  ColliderWorldTransformTest.cpp
  TransformHierarchyTest.cpp
  ColliderBindingsTest.cpp
  WorldBindingsTest.cpp
  ModelRendererBindingsTest.cpp
  TransformRigidBodyBindingsTest.cpp
  LightRendererTest.cpp
  LightRendererBindingsTest.cpp
  PlayerControllerBindingsTest.cpp
  LogTest.cpp
  LogFilterTest.cpp
  LogBindingsTest.cpp
  ScriptFieldAssetTest.cpp
  ScriptFieldEditTest.cpp
  ScriptDirectoriesTest.cpp
  ScriptFieldLabelTest.cpp
  AttachedScriptsTest.cpp
  ScriptClassAssetsTest.cpp
  ScriptSystemLifecycleTest.cpp
  ScriptSystemSceneScriptTest.cpp
  ScriptSystemSceneEventsTest.cpp
  ScriptSystemFieldsAndReloadTest.cpp
  UnresolvedScriptsTest.cpp
  ScriptBindingGapsTest.cpp
  CollisionEventTest.cpp
  CompoundBodyCollisionTest.cpp
  CompoundFlushTest.cpp
  CompoundRollingArmTest.cpp
  SettingsStoreTest.cpp
  RecentProjectsTest.cpp
  RecentProjectRecordingTest.cpp
  FileSinkTest.cpp
  LogFilePathTest.cpp
  RemoteLogSinkTest.cpp
  KeybindsTest.cpp
  KeybindRebindFlowTest.cpp
  BillboardTest.cpp
  ThumbnailImageTest.cpp
  LightGizmoVisibilityTest.cpp
  PlayControlsTest.cpp
  GridVisibilityTest.cpp
  EditorViewportSettingsTest.cpp
  GizmoTest.cpp
  GizmoEditSendingTest.cpp
  GizmoDragTest.cpp
  GizmoDragParentedTest.cpp
  EditorSelectionTest.cpp
  TransientObjectTest.cpp
  PrefabBodyEditsTest.cpp
  ObjectSpawnTest.cpp
  ObjectDestroyedTest.cpp
  SerializationRoundTripTest.cpp
  ProjectSerializerLogTest.cpp
  StateDeltaTest.cpp
  StatusPayloadTest.cpp
  SceneDeleteTest.cpp
  SceneManagerTest.cpp
  SceneRenameTest.cpp
  SceneScriptListTest.cpp
  SceneScriptEditTest.cpp
  SceneScriptUndoTest.cpp
  SceneScriptsPanelTest.cpp
  AssetNamingTest.cpp
  AssetCreateFlowTest.cpp
  ObjectNameFieldTest.cpp
  PrefabTest.cpp
  FixedTimestepTest.cpp
  InertiaTensorBehaviorTest.cpp
  InertiaTensorTests.cpp
  PhysicsIntegrationTest.cpp
  PhysicsLandingTest.cpp
  PhysicsSlopeTest.cpp
  PhysicsGenericRestingParityTest.cpp
  PhysicsGenericRestingShapesTest.cpp
  FirstPersonTemplateTest.cpp
  FirstPersonTemplateAssetsTest.cpp
  PhysicsPlaygroundSampleTest.cpp
  PhysicsPlaygroundSamplePhysicsTest.cpp
  PhysicsPlaygroundSamplePyramidTest.cpp
  PhysicsPlaygroundSampleRaceTest.cpp
  PhysicsPlaygroundSampleSpawnerTest.cpp
  PhysicsPlaygroundTestHelpers.h
  PlanarHullTest.cpp
  PresetsTest.cpp
  AgentDocsTest.cpp
  AppIconTest.cpp
  TemplateManifestTest.cpp
  ProjectFingerprintTest.cpp
  ProjectIdentityCheckTest.cpp
  ProjectLoadTrackerTest.cpp
  ProjectOriginTest.cpp
  ProjectPathsTest.cpp
  TemplateValidation.h
  TemplateValidationTest.cpp
  PhysicsStackingTest.cpp
  PhysicsTippingTest.cpp
  PhysicsTwoBodyTest.cpp
  PhysicsContactResponseTest.cpp
  PhysicsFrictionHoldTest.cpp
  PhysicsSlopeStackTest.cpp
  DefaultProjectTest.cpp
  EmptyTemplateTest.cpp
  DefaultShapesTest.cpp
  DefaultWelcomePhysicsTest.cpp
  DefaultWelcomeTest.cpp
  ShapeDropTest.cpp
  RuntimeComponentTest.cpp
  ScriptComponentOpsTest.cpp
  ObjectComponentsChangedTest.cpp
  RuntimeObjectTest.cpp
  ObjectHierarchyTest.cpp
  MultiSelectEditTest.cpp
  ObjectManagerLifecycleTest.cpp
  ObjectManagerInstantiationTest.cpp
  ObjectManagerScriptPassTest.cpp
  SceneEditValidationTest.cpp
  SceneEditReparentTest.cpp
  SceneEditRestoreTest.cpp
  SceneEditRestoreAdoptTest.cpp
  SceneEditCreateAndReorderTest.cpp
  SceneEditBatchTest.cpp
  SceneEditFailurePathsTest.cpp
  ProtocolFramingTest.cpp
  ServerPolicyTest.cpp
  StructuralChangesTest.cpp
  ShutdownSignalTest.cpp
  ChildProcessTest.cpp
  EditTokenTest.cpp
  EditorConnectionsTest.cpp
  CommandLineSplitTest.cpp
  ServerArgumentsTest.cpp
  ClientConnectionStateTest.cpp
  FakeTransports.h
  NetClientTest.cpp
  NetServerTest.cpp
  InputStatePayloadTest.cpp
  InputFocusTest.cpp
  InputViewTest.cpp
  InputStateTest.cpp
  InputUtilsBindingsTest.cpp
  PlayerSlotsTest.cpp
  WireTypesTest.cpp
  WireTruncationSweepTest.cpp
  WireTruncation.h
  WireTruncationFixtures.h
  MessageQueueTest.cpp
  MissingAssetFallbackTest.cpp
  TransportLogTest.cpp
  SharedAssetLoadsTest.cpp
  SmokeTest.cpp
  TestScene.cpp
  TestPrinters.h
  TestScene.h
  EditHistoryFixtures.h
  ObjectManagerFixtures.h
  SceneEditFixtures.h
  RecordEditsFixtures.h
  GizmoFixtures.h
  PhysicsTestHelpers.h
  PhysicsGenericRestingFixtures.h

  # The CLR-free pieces of ECS3DNet are compiled in rather than linked: linking the library would pull
  # ManagedHost and the C# transport assembly into a suite that is headless by construction. That is
  # also the constraint the arrangement rests on - do not add ECS3DNet to the link libraries below, or
  # these objects and the library's duplicate. A net source joins them only if it makes no CLR call; the
  # CLR calls stay in ManagedServerTransport and ManagedClientTransport. TransportLog.cpp and ServerLog.cpp are
  # compiled in the same way, since both depend only on ECS3DLog + ECS3DNetProtocol. CommandLineSplit.cpp,
  # ClientConnectionState.cpp and EditorConnections.cpp are the CLR-free pieces split out of ServerProcess,
  # NetClient and NetServer. NetServer.cpp and NetClient.cpp compile in too: they reach the C# transport
  # only through ServerTransport/ClientTransport, and the CLR-backed ManagedServerTransport and
  # ManagedClientTransport stay out (the tests drive a fake from FakeTransports.h).
  ../libs/net/MessageQueue.cpp
  ../libs/net/ChildProcess.cpp
  ../libs/net/CommandLineSplit.cpp
  ../libs/net/ClientConnectionState.cpp
  ../libs/net/EditorConnections.cpp
  ../libs/net/NetServer.cpp
  ../libs/net/NetClient.cpp
  # The Transform/RigidBody/Camera/InputUtils binding providers (and InputState, which InputUtilsBindings
  # reads) depend only on ECS3DData, so they are compiled in directly for the same reason as LogBindings.
  ../libs/scripting/bindings/TransformBindings.cpp
  ../libs/scripting/bindings/RigidBodyBindings.cpp
  ../libs/scripting/bindings/CameraBindings.cpp
  ../libs/scripting/bindings/InputUtilsBindings.cpp
  ../libs/scripting/bindings/InputState.cpp
  ../libs/net/ServerLog.cpp
  ../libs/scripting/bindings/ComponentOpsBindings.cpp
  ../libs/net/TransportLog.cpp
  # The asset reference count depends only on ECS3DData and json, so it is compiled in the same way.
  ../libs/editor/AssetReferences.cpp
  ../libs/scripting/bindings/ColliderBindings.cpp
  ../libs/scripting/bindings/WorldBindings.cpp
  ../libs/scripting/bindings/ModelRendererBindings.cpp

  # LogBindings depends only on ECS3DLog (no data/CLR includes), so it is compiled in directly rather
  # than pulling all of ECS3DScripting - and its ManagedHost/CLR dependency - into a headless suite.
  ../libs/scripting/bindings/LogBindings.cpp

  # BindingContext and the component binding providers depend only on ECS3DData, so they are compiled in
  # directly for the same reason as LogBindings.
  ../libs/scripting/bindings/BindingContext.cpp

  # ScriptFieldEdit depends only on json, so it is compiled in the same way.
  ../libs/scripting/ScriptFieldEdit.cpp
  ../libs/scripting/AttachedScripts.cpp
  ../libs/scripting/ScriptSystem.cpp
  ../libs/scripting/UnresolvedScripts.cpp
  ../libs/scripting/bindings/LightRendererBindings.cpp
  ../libs/scripting/bindings/PlayerControllerBindings.cpp

  # The server's built-in project is plain JSON over json, glm and uuid, so it is compiled in the same way
  # rather than linking the server app.
  ../apps/server/DefaultProject.cpp
  ../apps/server/DefaultScenes.cpp
  ../apps/server/DefaultShapes.cpp
  ../apps/server/DefaultWelcome.cpp
  ../apps/server/RestPose.cpp
  ../apps/server/ShutdownSignal.cpp

  # The gizmo core depends only on glm, so it is compiled in the same way rather than linking the whole
  # of ECS3DEditorLib (which pulls in ImGui/Vulkan through ECS3DRender).
  ../libs/editor/Gizmo.cpp

  # The gizmo's edit sending depends only on ECS3DData, Gizmo.h and json.
  ../libs/editor/GizmoEditSending.cpp

  # TransientObject depends only on ECS3DData and json.
  ../libs/editor/TransientObject.cpp

  # The convex hull rebuild step depends only on ECS3DData, glm and ECS3DLog.
  ../libs/editor/components/ConvexHullRebuild.cpp

  # The billboard math depends only on glm, compiled in rather than linking ECS3DRender, which pulls in
  # Vulkan.
  ../libs/render/Billboard.cpp

  # The focus rule and its gating are plain logic; only capture() needs GLFW and ImGui.
  ../libs/render/InputFocus.cpp

  # The view-block helper takes plain numbers and a uuid, so it compiles in without the capture code that
  # needs GLFW.
  ../libs/render/InputView.cpp

  # The thumbnail letterbox and PNG encode depend only on the standard library and stb_image_write.
  ../libs/render/ThumbnailImage.cpp
)


# stb is fetched by VulkanEngine, which libs/ configures before this directory. FetchContent_MakeAvailable
# sets stb_SOURCE_DIR only in the scope that called it, so read the global FetchContent property instead.
FetchContent_GetProperties(stb SOURCE_DIR ECS3D_STB_SOURCE_DIR)
if(NOT ECS3D_STB_SOURCE_DIR)
  message(FATAL_ERROR "stb was not fetched; ECS3DTests needs it for ThumbnailImage")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
  ${ECS3D_STB_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../apps/server
  ${CMAKE_CURRENT_SOURCE_DIR}/../libs/net
  ${CMAKE_CURRENT_SOURCE_DIR}/../libs/scripting/bindings
  ${CMAKE_CURRENT_SOURCE_DIR}/../libs/scripting
  ${CMAKE_CURRENT_SOURCE_DIR}/../libs/editor
  ${CMAKE_CURRENT_SOURCE_DIR}/../libs/render
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DData
  ECS3DLog
  ECS3DNetProtocol
  ECS3DSettings
  ECS3DSim
  GTest::gtest_main
  Threads::Threads
)

# Kept out of bin/, which is the shipped executable layout.
set_target_properties(${PROJECT_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/tests
)

# PresetsTest and AgentDocsTest read CMakePresets.json and the agent docs from the repo root, which is
# CMAKE_SOURCE_DIR since this directory is only added when ECS3D is top-level.
target_compile_definitions(${PROJECT_NAME} PRIVATE ECS3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(GoogleTest)

# Under the sanitize and coverage presets every process pays a large fixed startup cost (the sanitizer
# runtime, coverage counters), so one process per case spends most of the run starting up. In that mode
# the suite registers one CTest test per GoogleTest suite instead, and the cases of a suite share a
# process. IDE test explorers want per-case entries, so every other build keeps gtest_discover_tests.
if(ECS3D_SANITIZE OR ECS3D_COVERAGE)
  set(ECS3D_TEST_PER_SUITE_DEFAULT ON)
else()
  set(ECS3D_TEST_PER_SUITE_DEFAULT OFF)
endif()
option(ECS3D_TEST_PER_SUITE "Register one CTest test per GoogleTest suite instead of one per case"
  ${ECS3D_TEST_PER_SUITE_DEFAULT})

if(ECS3D_TEST_PER_SUITE)
  # Multi-config generators get one file per configuration, picked by the configuration ctest runs with
  # (-C, which the check target passes). A single-config build uses one fixed name, since ctest run
  # without -C leaves CTEST_CONFIGURATION_TYPE empty.
  if(CMAKE_CONFIGURATION_TYPES)
    set(ECS3D_SUITES_FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}_suites_$<CONFIG>.cmake")
    set(ECS3D_SUITES_FILE_SUFFIX "_\${CTEST_CONFIGURATION_TYPE}")
  else()
    set(ECS3D_SUITES_FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}_suites.cmake")
    set(ECS3D_SUITES_FILE_SUFFIX "")
  endif()

  # Same 120 second allowance as DISCOVERY_TIMEOUT in the per-case branch below, for the same reason. The
  # working directory is gtest_discover_tests' default, so both modes run tests from the same place.
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -D "TEST_EXECUTABLE=$<TARGET_FILE:${PROJECT_NAME}>"
      -D "OUTPUT_FILE=${ECS3D_SUITES_FILE}"
      -D "WORKING_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
      -D "TIMEOUT=120"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RegisterTestSuites.cmake"
    BYPRODUCTS "${ECS3D_SUITES_FILE}"
    COMMENT "Registering ${PROJECT_NAME} test suites"
    VERBATIM
  )

  # Before the first build the file does not exist, so ctest sees a failing placeholder test that
  # says the suite is not built.
  set(ECS3D_SUITES_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}_suites_include.cmake")
  file(WRITE "${ECS3D_SUITES_INCLUDE}"
    "set(suitesFile \"${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}_suites${ECS3D_SUITES_FILE_SUFFIX}.cmake\")\n"
    "if(EXISTS \"\${suitesFile}\")\n"
    "  include(\"\${suitesFile}\")\n"
    "else()\n"
    "  add_test(${PROJECT_NAME}_NOT_BUILT ${PROJECT_NAME}_NOT_BUILT)\n"
    "endif()\n")
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${ECS3D_SUITES_INCLUDE}")
else()
  # DISCOVERY_TIMEOUT is raised from the five second default because discovery runs the freshly linked test
  # binary to list its cases, and a cold Windows Debug executable with a large PDB can spend longer than
  # that just starting. It surfaces as the *build* failing with "Process terminated due to timeout" and no
  # output, which reads like a test hanging rather than a binary that had not finished loading - and it is
  # borderline rather than deterministic, so it comes and goes with the size of the suite.
  gtest_discover_tests(${PROJECT_NAME} DISCOVERY_TIMEOUT 120)
endif()

# Runs in every configuration so the suite parser is proven by the ordinary rows before the sanitize
# and coverage rows depend on it.
add_test(
  NAME ECS3DTestSuiteRegistration
  COMMAND ${CMAKE_COMMAND}
    -D "REGISTER_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/cmake/RegisterTestSuites.cmake"
    -D "WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/suite-registration"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RegisterTestSuitesTest.cmake"
)

# Ninja and Makefile configures emit compile_commands.json when the top-level project enables the export.
# sha256sum fails on a missing file and prints one short line, unlike cat of a multi-megabyte database.
if(CMAKE_EXPORT_COMPILE_COMMANDS AND CMAKE_GENERATOR MATCHES "^(Ninja|Unix Makefiles)$")
  add_test(
    NAME ECS3DCompileCommandsExported
    COMMAND ${CMAKE_COMMAND} -E sha256sum "${CMAKE_BINARY_DIR}/compile_commands.json"
  )
endif()

# The managed (C#) test project, run through `dotnet test` rather than a native executable - registered
# as its own CTest test so `check` (and CI) covers the managed assemblies the same way it covers the
# native ones. Guarded on DOTNET_EXE (set by find_package(Dotnet REQUIRED), which every
# managed-assembly-owning lib already pulls in via clrHost/cmake/loadCS.cmake by the time this directory
# is processed - libs/ is added before tests/ in source/CMakeLists.txt) rather than unconditionally,
# mirroring how the suite above is gated on BUILD_TESTING: a configure that somehow never reached the
# managed toolchain skips this test instead of failing here on an unset DOTNET_EXE.
if(DOTNET_EXE)
  set(ECS3D_MANAGED_TESTS_ROOT "${CMAKE_BINARY_DIR}/ECS3DManagedTests")
  set(ECS3D_MANAGED_TESTS_CSPROJ "${CMAKE_CURRENT_SOURCE_DIR}/managed/ECS3DManagedTests.csproj")

  # `dotnet test` both builds and runs the suite in one command, and nothing here runs at CMake build
  # time - only when ctest (directly, or through `check`) runs this test. That is what keeps a plain
  # `cmake --build` from ever invoking a build-time `dotnet build`/`test` on the C# tree (AGENTS.md says
  # never to do that): this project is built only as a side effect of ctest running it, on whichever
  # machine runs the test step, so there is nothing to chain into ECS3DManaged.cmake's serialized
  # managed-assembly targets. -p:BuildRoot isolates its obj/bin under $(BuildRoot)obj/tests/ and
  # $(BuildRoot)bin/tests/ (see source/tests/managed/Directory.Build.props) from every other managed
  # build in the tree, the same way
  # ecs3d_add_managed_assembly() isolates ScriptBridge/ECS3DNetTransport from each other and from a
  # developer's own `dotnet` invocations - the isolation CS0579 depends on.
  set(ECS3D_MANAGED_TESTS_ARGS
    -c Release
    -p:BuildRoot=${ECS3D_MANAGED_TESTS_ROOT}/
    --nologo
  )

  # The collector ships with Microsoft.NET.Test.Sdk, which the test project already references. The
  # semicolon is escaped because this is a CMake list: unescaped, it splits the collector argument in two.
  if(ECS3D_COVERAGE)
    list(APPEND ECS3D_MANAGED_TESTS_ARGS
      --collect "Code Coverage\;Format=cobertura"
      --results-directory ${CMAKE_BINARY_DIR}/coverage/managed
    )
  endif()

  add_test(
    NAME ECS3DManagedTests
    COMMAND ${DOTNET_EXE} test "${ECS3D_MANAGED_TESTS_CSPROJ}" ${ECS3D_MANAGED_TESTS_ARGS}
  )

  # A parallel ctest run starts tests in descending cost order. With no recorded timings (CI never has
  # them) this ~30 s test would otherwise start last and become the run's tail.
  set_tests_properties(ECS3DManagedTests PROPERTIES
    LABELS "managed"
    COST 1000
  )
endif()

# Builds the suite and runs it through CTest in one command. --test-dir keeps it to this suite rather
# than every test in the tree. --no-tests=error is the difference between a red build and a green one
# when nothing registers: ctest exits 0 on an empty test set, which is how this step reported success
# before the suite was wired up. COMMAND_EXPAND_LISTS is what splits the conditional --build-config into
# two arguments; without it the pair arrives as one semicolon-joined argument that ctest rejects. The
# suite runs in parallel because most of its time is per-process startup across the ~2,200 test cases,
# which the per-suite registration above cuts to one process per suite.
add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_CURRENT_BINARY_DIR}" --output-on-failure
    --no-tests=error --parallel "$<$<BOOL:$<CONFIG>>:--build-config;$<CONFIG>>"
  USES_TERMINAL
  VERBATIM
  COMMAND_EXPAND_LISTS
)

add_dependencies(check ${PROJECT_NAME})
