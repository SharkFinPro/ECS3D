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

# ModelRenderer rendering is done (RenderSystem + GpuAssetCache).
# TODO: light submission still lives as a TODO in RenderSystem (LightRenderer -> vke::PointLight/
# TODO:   SpotLight, cached per component), and the collider debug gizmo needs porting here too.
