include(FetchContent)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/clrHost/cmake")

# Shared, render-free dependencies. Declared here so every library below can link them
# directly instead of going through ECS3DCore. Re-declaring the same names in core is a no-op.
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
)

# glm is header-only (no Vulkan), so ECS3DData can hold vector/matrix fields without depending on
# the renderer. We declare it ourselves (rather than reuse VulkanEngine's) so the render-free libs
# don't transitively pull in the renderer just to use glm. This tag MUST stay in sync with the glm
# version VulkanEngine fetches (currently 1.0.1, in its source/cmake/External.cmake): FetchContent is
# first-wins and we declare first, so this copy becomes the single glm target both sides resolve to.
# If they diverge, bump this to match VulkanEngine's tag.
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

# glm (1.0.1) and stduuid (v1.2.3) each carry their own cmake_minimum_required() below 3.10, which
# trips CMake's "Compatibility with CMake < 3.10 will be removed" deprecation warning (added in
# CMake 3.31: https://cmake.org/cmake/help/latest/command/cmake_minimum_required.html). It does not
# yet risk the hard error CMake 4.0 added, since that only fires below 3.5
# (https://cmake.org/cmake/help/latest/command/cmake_minimum_required.html#cmake-4-0), but a future
# CMake raising that floor to 3.10 would break the configure. CMAKE_POLICY_VERSION_MINIMUM
# (https://cmake.org/cmake/help/latest/variable/CMAKE_POLICY_VERSION_MINIMUM.html) raises the policy
# version FetchContent hands them without touching their own files, heading that off; it is a no-op
# on CMake < 4.0, where the variable does not exist yet, so CMAKE_WARN_DEPRECATED also covers today's
# warning on those versions. Both are scoped to this call so our own subdirectories below keep seeing
# deprecation warnings.
set(CMAKE_WARN_DEPRECATED OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

FetchContent_MakeAvailable(
  json
  glm
  uuid
  nfd
  VulkanEngine
)

unset(CMAKE_WARN_DEPRECATED)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# Captured to cache so the render/editor subdirectories (configured after this) can add the include
# path the <VulkanEngine/...> headers need.
set(ECS3D_VULKANENGINE_INCLUDE "${VulkanEngine_SOURCE_DIR}/include" CACHE INTERNAL "VulkanEngine include dir")

add_subdirectory(log)
add_subdirectory(protocol)
add_subdirectory(settings)
add_subdirectory(data)
add_subdirectory(clrHost)
add_subdirectory(net)
add_subdirectory(sim)
add_subdirectory(scripting)
add_subdirectory(render)
add_subdirectory(editor)
