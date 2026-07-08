project(ECS3DData)

add_library(${PROJECT_NAME}
  ComponentRegistry.cpp
  ComponentRegistry.h
  ComponentRegistration.cpp
  ComponentRegistration.h
  ProjectSerializer.cpp
  ProjectSerializer.h
  ProjectPacker.cpp
  ProjectPacker.h
  Replication.cpp
  Replication.h
  assets/AssetRegistry.cpp
  assets/AssetRegistry.h
  scenes/SceneManager.cpp
  scenes/SceneManager.h
  scenes/SceneAsset.cpp
  scenes/SceneAsset.h
  objects/Object.cpp
  objects/Object.h
  objects/ObjectManager.cpp
  objects/ObjectManager.h
  objects/components/Component.cpp
  objects/components/Component.h
  objects/components/Transform.cpp
  objects/components/Transform.h
  objects/components/RigidBody.cpp
  objects/components/RigidBody.h
  objects/components/Script.cpp
  objects/components/Script.h
  objects/components/PlayerController.cpp
  objects/components/PlayerController.h
  objects/components/ModelRenderer.cpp
  objects/components/ModelRenderer.h
  objects/components/LightRenderer.cpp
  objects/components/LightRenderer.h
  objects/components/collisions/Collider.cpp
  objects/components/collisions/Collider.h
  objects/components/collisions/BoxCollider.cpp
  objects/components/collisions/BoxCollider.h
  objects/components/collisions/SphereCollider.cpp
  objects/components/collisions/SphereCollider.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  nlohmann_json::nlohmann_json
  glm::glm
  stduuid
  ECS3DNetProtocol
)

# ECS3DData is feature-complete: all component data, Object/ObjectManager, scenes (SceneManager/
# SceneAsset), the AssetRegistry, and the ProjectSerializer live here, with no Vulkan/imgui includes.
# The remaining migration work is in the systems/editor/net/clrHost libs and the app wiring.
