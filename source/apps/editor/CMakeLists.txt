project("ECS3DEditor")

add_executable(${PROJECT_NAME}
  main.cpp
  EditorApp.cpp
  EditorApp.h
)

# The editor is a client + tooling. Like the client it renders and connects over the network; it
# adds ECS3DEditorLib. It does NOT link ECS3DSim or ECS3DScripting (the spawned --edit server does).
target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DData
  ECS3DRender
  ECS3DEditorLib
  ECS3DNet
  ECS3DClrHost
)

# TODO: call ecs3d_deploy_clr_runtime(${PROJECT_NAME}) once it exists, to copy nethost.dll.
