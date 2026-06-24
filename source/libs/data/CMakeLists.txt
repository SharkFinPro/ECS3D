project(ECS3DData)

add_library(${PROJECT_NAME}
  ComponentRegistry.cpp
  ComponentRegistry.h
  context/SimContext.h
  context/RenderContext.h
  context/EditContext.h
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
)

# TODO: migrate the remaining plain-data + serialize() halves of the core types into this library:
# TODO:   objects/Object, objects/ObjectManager, the data fields of ModelRenderer/LightRenderer/Script,
# TODO:   the collider shape data (Collider/BoxCollider/SphereCollider bounds), scenes/SceneManager,
# TODO:   the asset registry half of AssetManager (-> AssetRegistry), and the load/save-path half of
# TODO:   SaveManager (-> ProjectSerializer). Each concrete component inherits the data-only Component.

# TODO: link stduuid once Object/ObjectManager move here (they key objects by uuid).
