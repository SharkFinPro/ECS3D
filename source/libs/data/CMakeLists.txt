project(ECS3DData)

add_library(${PROJECT_NAME}
  ComponentRegistry.cpp
  ComponentRegistry.h
  ComponentRegistration.cpp
  ComponentRegistration.h
  context/SimContext.h
  context/RenderContext.h
  context/EditContext.h
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
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  nlohmann_json::nlohmann_json
  glm::glm
  stduuid
)

# TODO: migrate the remaining plain-data + serialize() halves of the core types into this library:
# TODO:   the data fields of ModelRenderer/LightRenderer/Script, the collider shape data
# TODO:   (Collider/BoxCollider/SphereCollider bounds), scenes/SceneManager, the asset registry half
# TODO:   of AssetManager (-> AssetRegistry), and the load/save-path half of SaveManager
# TODO:   (-> ProjectSerializer). Each concrete component inherits the data-only Component.
