project(ECS3DRender)

add_library(${PROJECT_NAME}
  RenderSystem.cpp
  RenderSystem.h
  GpuAssetCache.cpp
  GpuAssetCache.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${ECS3D_VULKANENGINE_INCLUDE}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  ECS3DData
  VulkanEngine
)

# RenderSystem is functionally complete: ModelRenderer drawing (via GpuAssetCache), LightRenderer
# point/spot light submission (cached per object), and the pick-feedback bool.
# TODO: only the collider debug gizmo remains (default-off viz) — see the TODO in RenderSystem.cpp.
