project(ECS3DClrHost)

add_library(${PROJECT_NAME}
  ManagedHost.cpp
  ManagedHost.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Native CLR hosting (nethost/hostfxr). Linked PUBLIC so ECS3DNet and ECS3DScripting
# inherit it transitively and never call find_package(Dotnet) themselves.
include(cmake/loadCS.cmake)
