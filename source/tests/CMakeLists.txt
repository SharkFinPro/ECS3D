project(ECS3DTests)

include(FetchContent)

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
  LightRendererTest.cpp
  SmokeTest.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DData
  GTest::gtest_main
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
# than every test in the tree. COMMAND_EXPAND_LISTS is what splits the conditional --build-config into
# two arguments; without it the pair arrives as one semicolon-joined argument that ctest rejects.
add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_CURRENT_BINARY_DIR}" --output-on-failure
    "$<$<BOOL:$<CONFIG>>:--build-config;$<CONFIG>>"
  USES_TERMINAL
  VERBATIM
  COMMAND_EXPAND_LISTS
)

add_dependencies(check ${PROJECT_NAME})
