project("ECS3DServer")

add_executable(${PROJECT_NAME}
  main.cpp
  ServerApp.cpp
  ServerAppBroadcast.cpp
  ServerAppEdits.cpp
  ServerAppMessages.cpp
  ServerAppProject.cpp
  ServerApp.h
  DefaultProject.cpp
  DefaultProject.h
  DefaultProjectParts.h
  DefaultShapes.cpp
  DefaultShapes.h
  DefaultWelcome.cpp
  DefaultWelcome.h
  ServerPolicy.h
)

# The server does all the number crunching. It is headless: it must NOT link ECS3DRender or
# ECS3DEditorLib.
target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DData
  ECS3DSim
  ECS3DScripting
  ECS3DNet
  ECS3DClrHost
  ECS3DLog
)

# Copy nethost.dll next to the executable so it can boot the CLR (the C# transport + ScriptBridge).
ecs3d_deploy_clr_runtime(${PROJECT_NAME})

# The sample project is generated in code (DefaultProject) rather than shipped as a file, so there's no
# project to deploy next to the executable. Its models/textures (defaultAssets) DO need to ship next to
# the executable, though: the renderer (editor/client, which share this bin) resolves the project's
# asset paths against the working directory. Deployed at configure time so it doesn't depend on build
# order, and unconditionally (the apps need it whether or not the tests are built).
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/defaultAssets/" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/")

# Templates and samples ship next to the executable too (the launcher and editor read them from
# <exe dir>/templates), copied at configure time like defaultAssets.
# CONFIGURE_DEPENDS re-runs configure, and so the copy, when a template file is added or removed.
file(GLOB_RECURSE ECS3D_TEMPLATE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../templates/*")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../../templates/" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/templates/")
