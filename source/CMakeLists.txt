add_subdirectory(libs)
add_subdirectory(apps)

if (PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  add_subdirectory(tests)
endif()

# Precompiled headers: the standard and third-party headers most of the tree includes, ranked by
# #include count. Each target gets its own PCH because their compile definitions differ (VulkanEngine
# adds GLM_FORCE_DEPTH_ZERO_TO_ONE and the VULKAN_HPP_* set publicly). windows.h and GLFW stay out:
# four files define NOMINMAX/WIN32_LEAN_AND_MEAN/GLFW_INCLUDE_NONE before including them.
# A PCH hides a missing include, so the sanitize preset turns it off and its CI rows still build every
# file on its own includes.
option(ECS3D_PRECOMPILED_HEADERS "Build ECS3D targets with precompiled headers" ON)

if (ECS3D_PRECOMPILED_HEADERS)
  set(_ecs3d_pch_std
    <algorithm> <array> <chrono> <cmath> <cstddef> <cstdint> <filesystem> <functional> <limits>
    <memory> <optional> <stdexcept> <string> <unordered_map> <unordered_set> <utility> <vector>
  )
  set(_ecs3d_pch_data
    <nlohmann/json.hpp> <uuid.h> <glm/vec3.hpp> <glm/mat4x4.hpp> <glm/gtc/quaternion.hpp>
    <glm/gtc/matrix_transform.hpp>
  )
  set(_ecs3d_pch_render <vulkan/vulkan_raii.hpp> <imgui.h>)

  target_precompile_headers(ECS3DLog PRIVATE ${_ecs3d_pch_std})
  target_precompile_headers(ECS3DSettings PRIVATE ${_ecs3d_pch_std} <nlohmann/json.hpp>)

  foreach (_target ECS3DData ECS3DNet ECS3DSim ECS3DScripting ECS3DServer)
    target_precompile_headers(${_target} PRIVATE ${_ecs3d_pch_std} ${_ecs3d_pch_data})
  endforeach()

  foreach (_target ECS3DRender ECS3DEditorLib ECS3DClient ECS3DEditor)
    target_precompile_headers(${_target} PRIVATE ${_ecs3d_pch_std} ${_ecs3d_pch_data} ${_ecs3d_pch_render})
  endforeach()

  if (TARGET ECS3DTests)
    target_precompile_headers(ECS3DTests PRIVATE ${_ecs3d_pch_std} ${_ecs3d_pch_data} <gtest/gtest.h>)
  endif()
endif()
