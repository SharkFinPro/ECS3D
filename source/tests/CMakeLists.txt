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
  AssetRegistryTest.cpp
  ComponentEditTest.cpp
  ComponentVariableTest.cpp
  CollisionMathTest.cpp
  SceneQueryTest.cpp
  ColliderWorldTransformTest.cpp
  LightRendererTest.cpp
  SettingsStoreTest.cpp
  ObjectSpawnTest.cpp
  SerializationRoundTripTest.cpp
  StateDeltaTest.cpp
  SceneManagerTest.cpp
  RuntimeComponentTest.cpp
  RuntimeObjectTest.cpp
  ObjectHierarchyTest.cpp
  SceneEditTest.cpp
  ProtocolFramingTest.cpp
  WireTypesTest.cpp
  MessageQueueTest.cpp
  SmokeTest.cpp
  TestPrinters.h

  # net's MessageQueue is compiled in rather than linked: it is the one piece of ECS3DNet with no CLR
  # dependency, and linking the library would pull ManagedHost and the C# transport assembly into a
  # suite that is headless by construction. That is also the constraint the arrangement rests on - do
  # not add ECS3DNet to the link libraries below, or these objects and the library's duplicate. Anything
  # else in net that needs covering will hit the same wall.
  ../libs/net/MessageQueue.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../libs/net
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DData
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

gtest_discover_tests(${PROJECT_NAME})

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
