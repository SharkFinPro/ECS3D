include(FetchContent)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/clrHost/cmake")

# Reusable helper for libraries that ship a C# assembly (ECS3DScripting, ECS3DNet).
# Defined at this level so every lib subdirectory inherits the function.
include(ECS3DManaged.cmake)

# Shared, render-free dependencies. Declared here so every library below can link them
# directly instead of going through ECS3DCore. Re-declaring the same names in core is a no-op.
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
)

# glm is header-only (no Vulkan), so ECS3DData can hold vector/matrix fields without the renderer.
# TODO: pin this to the same glm version VulkanEngine fetches to avoid a duplicate/mismatched target.
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE
)

# stduuid: Object/ObjectManager key objects by uuid.
FetchContent_Declare(
  uuid
  GIT_REPOSITORY https://github.com/mariusbancila/stduuid.git
  GIT_TAG v1.2.3
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(
  json
  glm
  uuid
)

# TODO: hoist the remaining FetchContent declarations (nfd, VulkanEngine) here too as the
# TODO:   render/editor types that need them migrate (VulkanEngine for ECS3DRender, nfd for
# TODO:   ECS3DEditorLib).

add_subdirectory(data)
add_subdirectory(clrHost)
add_subdirectory(net)
add_subdirectory(sim)
add_subdirectory(scripting)
add_subdirectory(render)
add_subdirectory(editor)
