project(ECS3DScripting)

add_library(${PROJECT_NAME}
  ScriptSystem.cpp
  ScriptSystem.h
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

# TODO: migrate ScriptManager + the script-call half of ScriptEngine here (attach/detach/start/
# TODO:   stop/fixedUpdate/variableUpdate + the exposed-field getters/setters), plus the component
# TODO:   script bindings (RigidBody/Transform/InputUtils) reading networked InputState. Server only.

# Build + deploy the C# ScriptBridge assembly (gameplay).
# TODO: relocate scripts/ScriptBridge out of source/core into this library, then update the path.
ecs3d_add_managed_assembly(${PROJECT_NAME}
  "${CMAKE_CURRENT_SOURCE_DIR}/../../core/scripts/ScriptBridge"
  "scripts/ScriptBridge"
  "ScriptBridge"
)

# TODO: copy scripts/UserScripts next to the executable (was in core/cmake/loadCS.cmake).
