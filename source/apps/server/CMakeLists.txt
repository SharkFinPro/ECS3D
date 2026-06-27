project("ECS3DServer")

add_executable(${PROJECT_NAME}
  main.cpp
  ServerApp.cpp
  ServerApp.h
  DefaultProject.cpp
  DefaultProject.h
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

# The sample project is generated in code (DefaultProject) rather than shipped as a file, so there's no
# project to deploy next to the executable. Its models/textures (defaultAssets) DO need to ship next to
# the executable, though: the renderer (editor/client, which share this bin) resolves the project's
# asset paths against the working directory. Deployed at configure time so it doesn't depend on build
# order, and unconditionally (the apps need it whether or not the tests are built).
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/defaultAssets/" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/")
