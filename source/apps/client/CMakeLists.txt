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
  ECS3DLog
  ECS3DRender
  ECS3DNet
  ECS3DClrHost
  ECS3DLog
)

# The client has its own window, so hide the console by default; --console opens one on demand.
# MSVC-only: clang-cl/MinGW would get a GUI subsystem with no WinMain and fail to link.
if(MSVC)
  set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(${PROJECT_NAME} PRIVATE /ENTRY:mainCRTStartup)
endif()

# Copy nethost.dll next to the executable so it can boot the CLR (the C# transport).
ecs3d_deploy_clr_runtime(${PROJECT_NAME})

# The client spawns ECS3DServer as a separate process at runtime, so force it to be built whenever
# the client is built. (Requires server to be added before this dir; see ../CMakeLists.txt.)
add_dependencies(${PROJECT_NAME} ECS3DServer)
