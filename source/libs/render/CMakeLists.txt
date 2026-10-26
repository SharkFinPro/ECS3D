project(ECS3DRender)

add_library(${PROJECT_NAME}
  RenderSystem.cpp
  RenderSystem.h
  GpuAssetCache.cpp
  GpuAssetCache.h
  Billboard.cpp
  Billboard.h
  CapsuleGizmo.h
  LightGizmoVisibility.h
  GridVisibility.h
  HullGizmoMesh.h
  InputCapture.cpp
  InputCapture.h
  InputView.cpp
  InputView.h
  MissingAssetFallback.h
  OutlineColors.h
  SharedAssetLoads.h
  ThumbnailImage.cpp
  ThumbnailImage.h
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
# submission (cached per object), the pick-feedback bool + selected-object outline, the collider
# debug gizmo (the shape outlined when "Render Collider" is on), and the editor-only light gizmo (a
# camera-facing, constant-screen-size billboard per light, via drawLightGizmos).
