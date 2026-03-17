cmake_minimum_required(VERSION 3.29)
include(FetchContent)

project(ECS3DEngine)

set(CMAKE_CXX_STANDARD 20)

add_library(${PROJECT_NAME}
  ECS3D.cpp
  ECS3D.h
  SaveManager.cpp
  SaveManager.h
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
  objects/components/Script.cpp
  objects/components/Script.h
  objects/components/collisions/Simplex.cpp
  objects/components/collisions/Simplex.h
  objects/components/collisions/Collider.cpp
  objects/components/collisions/Collider.h
  objects/components/collisions/BoxCollider.cpp
  objects/components/collisions/BoxCollider.h
  objects/components/collisions/SphereCollider.cpp
  objects/components/collisions/SphereCollider.h
  scenes/SceneManager.cpp
  scenes/SceneManager.h
  scenes/Scene.cpp
  scenes/Scene.h
  scripts/InputUtils.cpp
  scripts/InputUtils.h
  scripts/ScriptEngine.cpp
  scripts/ScriptEngine.h
  scripts/ScriptManager.cpp
  scripts/ScriptManager.h
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

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  nfd
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
  GIT_TAG v1.3.0
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  uuid
  GIT_REPOSITORY https://github.com/mariusbancila/stduuid.git
  GIT_TAG v1.2.3
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  VulkanEngine
  GIT_REPOSITORY https://github.com/SharkFinPro/VulkanRenderer.git
  GIT_TAG main
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(
  json
  nfd
  uuid
  VulkanEngine
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  nlohmann_json::nlohmann_json
  nfd
  stduuid
  VulkanEngine
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${VulkanEngine_SOURCE_DIR}/include
)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)
endif()

include(cmake/loadCS.cmake)