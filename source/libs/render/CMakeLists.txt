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

target_link_libraries(${PROJECT_NAME} PUBLIC
  ECS3DData
  # TODO: VulkanEngine (carries glm + imgui). Once the FetchContent is hoisted to source/libs,
  # TODO:   link it here: target_link_libraries(${PROJECT_NAME} PUBLIC VulkanEngine).
)

# TODO: migrate the render behavior here: ModelRenderer/LightRenderer variableUpdate (the
# TODO:   renderObject / PointLight / SpotLight calls), Asset::load() GPU uploads, and the
# TODO:   GPU-load half of AssetManager (-> GpuAssetCache).
