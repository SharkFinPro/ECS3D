project(ECS3DScripting)

add_library(${PROJECT_NAME}
  ScriptSystem.cpp
  ScriptSystem.h
  ScriptEngine.cpp
  ScriptEngine.h
  bindings/BindingContext.cpp
  bindings/BindingContext.h
  bindings/TransformBindings.cpp
  bindings/TransformBindings.h
  bindings/RigidBodyBindings.cpp
  bindings/RigidBodyBindings.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  ECS3DData
  ECS3DClrHost
)

# The script-call half of ScriptEngine + the old ScriptManager bookkeeping now live here
# (ScriptEngine.cpp / ScriptSystem.cpp). Remaining script binding TODO: InputUtils, which must read the
# networked InputState (the headless server has no GLFW window) - deferred with networked input.

# Build + deploy the C# ScriptBridge assembly (gameplay).
# TODO: relocate scripts/ScriptBridge out of source/core into this library, then update the path.
ecs3d_add_managed_assembly(${PROJECT_NAME}
  "${CMAKE_CURRENT_SOURCE_DIR}/../../core/scripts/ScriptBridge"
  "scripts/ScriptBridge"
  "ScriptBridge"
)

# Copy the user scripts next to the executable so the bridge can compile them at runtime (the bridge's
# init scans scripts/UserScripts). Was core/cmake/loadCS.cmake.
# TODO: relocate scripts/UserScripts out of source/core into this library, then update the path.
add_custom_target(ECS3DUserScripts ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../../core/scripts/UserScripts"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/scripts/UserScripts"
  COMMENT "Copying UserScripts next to the executable"
  VERBATIM
)
add_dependencies(${PROJECT_NAME} ECS3DUserScripts)
