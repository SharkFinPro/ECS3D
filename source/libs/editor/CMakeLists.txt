project(ECS3DEditorLib)

add_library(${PROJECT_NAME}
  ComponentEditor.cpp
  ComponentEditor.h
  AssetBrowserPanel.cpp
  AssetBrowserPanel.h
  components/TransformEditor.cpp
  components/TransformEditor.h
  components/RigidBodyEditor.cpp
  components/RigidBodyEditor.h
  components/ModelRendererEditor.cpp
  components/ModelRendererEditor.h
  components/LightRendererEditor.cpp
  components/LightRendererEditor.h
  components/ColliderEditor.cpp
  components/ColliderEditor.h
  components/ScriptEditor.cpp
  components/ScriptEditor.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  ECS3DData
  ECS3DRender
  # TODO: nfd (native file dialogs) for the SaveUI half of SaveManager, plus imgui via VulkanEngine.
)

# TODO: migrate every component's displayGui() into ComponentEditor (the central componentEditor),
# TODO:   ObjectGUIManager, the GUI half of AssetManager (-> AssetBrowserPanel), GuiComponents.h,
# TODO:   and the file-dialog/keybind half of SaveManager (-> SaveUI). Editor only.
