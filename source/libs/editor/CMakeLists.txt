project(ECS3DEditorLib)

add_library(${PROJECT_NAME}
  ComponentEditor.cpp
  ComponentEditor.h
  GuiComponents.h
  ObjectGUIManager.cpp
  ObjectGUIManager.h
  AssetBrowserPanel.cpp
  AssetBrowserPanel.h
  AssetDragDrop.h
  SaveUI.cpp
  SaveUI.h
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
  ECS3DRender  # pulls imgui transitively via VulkanEngine, which the component editors + GuiComponents use
  # TODO: nfd (native file dialogs) for the SaveUI half of SaveManager.
)

# DONE: component displayGui() bodies -> ComponentEditor handlers (the central componentEditor),
# ObjectGUIManager (object tree + selected-object panel), and GuiComponents.h all live here now.
# TODO: still to migrate — the GUI half of AssetManager (-> AssetBrowserPanel: browser/creation popups/
# TODO:   drag-drop sources/menu widget) and the file-dialog/keybind half of SaveManager (-> SaveUI).
