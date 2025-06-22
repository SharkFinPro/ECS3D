cmake_minimum_required(VERSION 3.29)
include(FetchContent)

project(ECS3DEngine)

set(CMAKE_CXX_STANDARD 20)

add_library(${PROJECT_NAME}
  ECS3D.cpp
  ECS3D.h
  objects/ObjectManager.cpp
  objects/ObjectManager.h
  objects/Object.cpp
  objects/Object.h
  objects/components/Component.cpp
  objects/components/Component.h
  objects/components/Components.h
  objects/components/Transform.cpp
  objects/components/Transform.h
  objects/components/ModelRenderer.cpp
  objects/components/ModelRenderer.h
  objects/components/RigidBody.cpp
  objects/components/RigidBody.h
  objects/components/collisions/Simplex.cpp
  objects/components/collisions/Simplex.h
  objects/components/collisions/Collider.cpp
  objects/components/collisions/Collider.h
  objects/components/collisions/BoxCollider.cpp
  objects/components/collisions/BoxCollider.h
  objects/components/collisions/SphereCollider.cpp
  objects/components/collisions/SphereCollider.h
  objects/components/Player.cpp
  objects/components/Player.h
  scenes/SceneManager.cpp
  scenes/SceneManager.h
  scenes/Scene.cpp
  scenes/Scene.h
  objects/components/LightRenderer.cpp
  objects/components/LightRenderer.h
  assets/AssetManager.cpp
  assets/AssetManager.h
  assets/Asset.cpp
  assets/Asset.h
  assets/TextureAsset.cpp
  assets/TextureAsset.h
  assets/ModelAsset.cpp
  assets/ModelAsset.h
  objects/CollisionManager.cpp
  objects/CollisionManager.h
  objects/ObjectGUIManager.cpp
  objects/ObjectGUIManager.h
  objects/components/collisions/Polytope.cpp
  objects/components/collisions/Polytope.h
)

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

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)
endif()