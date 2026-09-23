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
  ComponentLookupTest.cpp
  CameraRangeTest.cpp
  ComponentRegistryTest.cpp
  AssetRegistryTest.cpp
  ComponentEditTest.cpp
  MissedComponentEditLogTest.cpp
  ComponentVariableTest.cpp
  NonFiniteValuesTest.cpp
  EditHistoryRoundTripTest.cpp
  EditHistoryLabelTest.cpp
  EditHistoryRefusalTest.cpp
  RecordEditsTest.cpp
  CollisionMathTest.cpp
  NarrowPhaseTest.cpp
  SceneQueryTest.cpp
  ColliderWorldTransformTest.cpp
  ColliderBindingsTest.cpp
  LightRendererTest.cpp
  LightRendererBindingsTest.cpp
  LogTest.cpp
  LogFilterTest.cpp
  LogBindingsTest.cpp
  ScriptFieldEditTest.cpp
  ScriptBindingGapsTest.cpp
  CollisionEventTest.cpp
  SettingsStoreTest.cpp
  FileSinkTest.cpp
  LogFilePathTest.cpp
  RemoteLogSinkTest.cpp
  KeybindsTest.cpp
  ObjectSpawnTest.cpp
  SerializationRoundTripTest.cpp
  ProjectSerializerLogTest.cpp
  StateDeltaTest.cpp
  SceneManagerTest.cpp
  PrefabTest.cpp
  PhysicsIntegrationTest.cpp
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
  SceneEditCreateAndReorderTest.cpp
  ProtocolFramingTest.cpp
  InputStatePayloadTest.cpp
  WireTypesTest.cpp
  MessageQueueTest.cpp
  TransportLogTest.cpp
  SmokeTest.cpp
  TestScene.cpp
  TestPrinters.h
  TestScene.h
  EditHistoryFixtures.h
  ObjectManagerFixtures.h
  SceneEditFixtures.h

  # net's MessageQueue is compiled in rather than linked: it is the one piece of ECS3DNet with no CLR
  # dependency, and linking the library would pull ManagedHost and the C# transport assembly into a
  # suite that is headless by construction. That is also the constraint the arrangement rests on - do
  # not add ECS3DNet to the link libraries below, or these objects and the library's duplicate. Anything
  # else in net that needs covering will hit the same wall. TransportLog.cpp and ServerLog.cpp are
  # compiled in the same way, since both depend only on ECS3DLog + ECS3DNetProtocol.
  ../libs/net/MessageQueue.cpp
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
  ../libs/scripting/bindings/ColliderBindings.cpp

  # LogBindings depends only on ECS3DLog (no data/CLR includes), so it is compiled in directly rather
  # than pulling all of ECS3DScripting - and its ManagedHost/CLR dependency - into a headless suite.
  ../libs/scripting/bindings/LogBindings.cpp

  # BindingContext and the component binding providers depend only on ECS3DData, so they are compiled in
  # directly for the same reason as LogBindings.
  ../libs/scripting/bindings/BindingContext.cpp

  # ScriptFieldEdit depends only on json, so it is compiled in the same way.
  ../libs/scripting/ScriptFieldEdit.cpp
  ../libs/scripting/bindings/LightRendererBindings.cpp
)


target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../libs/net
  ${CMAKE_CURRENT_SOURCE_DIR}/../libs/scripting/bindings
  ${CMAKE_CURRENT_SOURCE_DIR}/../libs/scripting
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
)

include(GoogleTest)

# DISCOVERY_TIMEOUT is raised from the five second default because discovery runs the freshly linked test
# binary to list its cases, and a cold Windows Debug executable with a large PDB can spend longer than
# that just starting. It surfaces as the *build* failing with "Process terminated due to timeout" and no
# output, which reads like a test hanging rather than a binary that had not finished loading - and it is
# borderline rather than deterministic, so it comes and goes with the size of the suite.
gtest_discover_tests(${PROJECT_NAME} DISCOVERY_TIMEOUT 120)

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
  add_test(
    NAME ECS3DManagedTests
    COMMAND ${DOTNET_EXE} test "${ECS3D_MANAGED_TESTS_CSPROJ}"
      -c Release
      -p:BuildRoot=${ECS3D_MANAGED_TESTS_ROOT}/
      --nologo
  )

  set_tests_properties(ECS3DManagedTests PROPERTIES
    LABELS "managed"
  )
endif()

# Builds the suite and runs it through CTest in one command. --test-dir keeps it to this suite rather
# than every test in the tree. --no-tests=error is the difference between a red build and a green one
# when nothing registers: ctest exits 0 on an empty test set, which is how this step reported success
# before the suite was wired up. COMMAND_EXPAND_LISTS is what splits the conditional --build-config into
# two arguments; without it the pair arrives as one semicolon-joined argument that ctest rejects.
add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_CURRENT_BINARY_DIR}" --output-on-failure
    --no-tests=error "$<$<BOOL:$<CONFIG>>:--build-config;$<CONFIG>>"
  USES_TERMINAL
  VERBATIM
  COMMAND_EXPAND_LISTS
)

add_dependencies(check ${PROJECT_NAME})
