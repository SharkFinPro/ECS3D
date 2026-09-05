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

FetchContent_MakeAvailable(googletest)

add_executable(${PROJECT_NAME}
  RuntimeComponentTest.cpp
  SmokeTest.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DData
  GTest::gtest_main
)

include(GoogleTest)

gtest_discover_tests(${PROJECT_NAME})

# Builds the suite and runs it through CTest in one command.
add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --build-config $<CONFIG>
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  USES_TERMINAL
)

add_dependencies(check ${PROJECT_NAME})
