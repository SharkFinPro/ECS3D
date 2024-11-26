cmake_minimum_required(VERSION 3.29)
include(FetchContent)

project(ECS3D)

set(CMAKE_CXX_STANDARD 20)

add_executable(ECS3D main.cpp)

# Declare VulkanEngine as a dependency
FetchContent_Declare(
  VulkanEngine
  GIT_REPOSITORY https://github.com/SharkFinPro/VulkanRenderer.git
  GIT_TAG main
)

# Make VulkanEngine available to your project
FetchContent_MakeAvailable(VulkanEngine)

# Link VulkanEngine to your target
target_link_libraries(${PROJECT_NAME} PRIVATE VulkanEngine)

# Include VulkanEngine headers
target_include_directories(${PROJECT_NAME} PRIVATE ${VulkanEngine_SOURCE_DIR}/include)