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
# warning on those versions there. CMAKE_WARN_DEPRECATED only reliably takes effect as a cache entry
# (a normal variable is not enough), so it is forced into the cache here and the caller's prior cache
# value, if any, is restored afterward rather than just unset. VulkanEngine is fetched separately,
# after restoring both variables: its own floor (3.27) is already above 3.10, and its deprecation
# warnings are the developer's own and should stay visible, not get masked by this guard. glm is
# still made available first, inside the guard, so our pinned copy keeps winning FetchContent's
# first-wins resolution before VulkanEngine's declare below runs.
if(DEFINED CACHE{CMAKE_WARN_DEPRECATED})
  set(_ecs3d_saved_warn_deprecated "${CMAKE_WARN_DEPRECATED}")
endif()
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

FetchContent_MakeAvailable(
  json
  glm
  uuid
  nfd
)

if(DEFINED _ecs3d_saved_warn_deprecated)
  set(CMAKE_WARN_DEPRECATED "${_ecs3d_saved_warn_deprecated}" CACHE BOOL "" FORCE)
  unset(_ecs3d_saved_warn_deprecated)
else()
  unset(CMAKE_WARN_DEPRECATED CACHE)
endif()
unset(CMAKE_POLICY_VERSION_MINIMUM)

FetchContent_MakeAvailable(VulkanEngine)

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
