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

# nfd (native file dialogs): the editor's SaveUI (and, later, AssetBrowserPanel import) opens OS
# open/save dialogs. Editor-only, but declared here with the other shared deps.
FetchContent_Declare(
  nfd
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
  GIT_TAG v1.3.0
  GIT_SHALLOW TRUE
)

# VulkanEngine: the renderer ECS3DRender (and, transitively, ECS3DEditorLib) builds against. It also
# pulls glm, but glm is declared above first, so the first-wins FetchContent uses our pinned copy.
FetchContent_Declare(
  VulkanEngine
  GIT_REPOSITORY https://github.com/SharkFinPro/VulkanRenderer.git
  GIT_TAG main
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(
  json
  glm
  uuid
  nfd
  VulkanEngine
)

# Captured to cache so the render/editor subdirectories (configured after this) can add the include
# path the <VulkanEngine/...> headers need.
set(ECS3D_VULKANENGINE_INCLUDE "${VulkanEngine_SOURCE_DIR}/include" CACHE INTERNAL "VulkanEngine include dir")

add_subdirectory(data)
add_subdirectory(clrHost)
add_subdirectory(net)
add_subdirectory(sim)
add_subdirectory(scripting)
add_subdirectory(render)
add_subdirectory(editor)
