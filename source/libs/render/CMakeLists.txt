project(ECS3DRender)

add_library(${PROJECT_NAME}
  RenderSystem.cpp
  RenderSystem.h
  GpuAssetCache.cpp
  GpuAssetCache.h
  InputCapture.cpp
  InputCapture.h
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

# RenderSystem is complete: ModelRenderer drawing (via GpuAssetCache), LightRenderer point/spot light
# submission (cached per object), the pick-feedback bool + selected-object highlight, and the collider
# debug gizmo (the shape drawn with the objectHighlight pipeline when "Render Collider" is on).
