# Find .NET (fails loudly with install instructions if missing)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Dotnet REQUIRED)

# Expose ecs3d_add_managed_assembly / ecs3d_deploy_clr_runtime to all consumers.
include("${CMAKE_CURRENT_LIST_DIR}/ECS3DManaged.cmake")

# nethost/hostfxr lets native code start the CoreCLR runtime. PUBLIC so anything that links
# ECS3DClrHost (ECS3DNet, ECS3DScripting) inherits the include + link without finding .NET.
target_include_directories(${PROJECT_NAME} PUBLIC
  ${NETHOST_INCLUDE}
)

target_link_libraries(${PROJECT_NAME} PUBLIC ${NETHOST_LIB})

# Linux needs dl and pthread for dlopen / the .NET runtime
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(${PROJECT_NAME} PUBLIC dl pthread)
endif()

# macOS needs CoreFoundation (hostfxr dependency)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_link_libraries(${PROJECT_NAME} PUBLIC "-framework CoreFoundation")
endif()

# nethost.dll must sit next to every executable that boots the runtime (all three apps, since they
# all link ECS3DClrHost via ECS3DNet). A static lib has no runtime output of its own, so each app
# deploys the shim itself by calling ecs3d_deploy_clr_runtime(<exe_target>) (defined in
# ECS3DManaged.cmake, included above). ECS3DScripting/ECS3DNet deploy their assemblies the same way
# via ecs3d_add_managed_assembly().
