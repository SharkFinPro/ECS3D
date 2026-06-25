project("ECS3DServer")

add_executable(${PROJECT_NAME}
  main.cpp
  ServerApp.cpp
  ServerApp.h
)

# The server does all the number crunching. It is headless: it must NOT link ECS3DRender or
# ECS3DEditorLib.
target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DData
  ECS3DSim
  ECS3DScripting
  ECS3DNet
  ECS3DClrHost
)

# Copy nethost.dll next to the executable so it can boot the CLR (the C# transport + ScriptBridge).
ecs3d_deploy_clr_runtime(${PROJECT_NAME})
