cmake_minimum_required(VERSION 3.29)
include(FetchContent)

project(ECS3DEngine)

set(CMAKE_CXX_STANDARD 20)

add_library(${PROJECT_NAME}
  ECS3D.cpp
  ECS3D.h)

# Declare VulkanEngine as a dependency
FetchContent_Declare(
  VulkanEngine
  GIT_REPOSITORY https://github.com/SharkFinPro/VulkanRenderer.git
  GIT_TAG main
)

# Make VulkanEngine available to your project
FetchContent_MakeAvailable(VulkanEngine)

# Link VulkanEngine to your target
target_link_libraries(${PROJECT_NAME} PUBLIC VulkanEngine)

# Include VulkanEngine headers
target_include_directories(${PROJECT_NAME} PUBLIC ${VulkanEngine_SOURCE_DIR}/include)