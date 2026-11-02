project(ECS3DProfile)

# Separate from ECS3D_PROFILE, which only sets compiler flags for external profilers.
option(ECS3D_PROFILER_SCOPES "Compile ECS3D_PROFILE_SCOPE timers into the build" ON)

add_library(${PROJECT_NAME}
  History.cpp
  History.h
  Lane.cpp
  Lane.h
  ProfileScope.cpp
  ProfileScope.h
  ProfileStats.cpp
  ProfileStats.h
  ScopeTree.cpp
  ScopeTree.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
  ECS3D_PROFILER_SCOPES=$<BOOL:${ECS3D_PROFILER_SCOPES}>
)
