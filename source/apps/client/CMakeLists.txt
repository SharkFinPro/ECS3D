project("ECS3DClient")

add_executable(${PROJECT_NAME}
  main.cpp
  ClientApp.cpp
  ClientApp.h
)

# The client is a lightweight view: it renders and sends input. It does NOT link ECS3DSim or
# ECS3DScripting (the server does the crunching).
target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DData
  ECS3DRender
  ECS3DNet
  ECS3DClrHost
)

# TODO: call ecs3d_deploy_clr_runtime(${PROJECT_NAME}) once it exists, to copy nethost.dll.
