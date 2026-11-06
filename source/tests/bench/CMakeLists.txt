project(ECS3DCollisionBench)

# A headless timing tool, not a test: it is left out of the default build and out of CTest. Build it with
#   cmake --build <build-dir> --target ECS3DCollisionBench
add_executable(${PROJECT_NAME} EXCLUDE_FROM_ALL
  BenchScenes.cpp
  BenchScenes.h
  CollisionBench.cpp
  ../TestScene.cpp
  ../TestScene.h
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# TestScene.cpp reports through GoogleTest's expectations, so the bench links gtest without running it.
target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DData
  ECS3DLog
  ECS3DProfile
  ECS3DSim
  GTest::gtest
)

# Beside the test suite rather than in bin/, which is the shipped executable layout.
set_target_properties(${PROJECT_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/tests
)
