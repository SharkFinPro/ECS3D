# Find .NET (fails loudly with install instructions if missing)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Dotnet REQUIRED)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${NETHOST_INCLUDE}
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${NETHOST_LIB})

# Linux needs dl and pthread for dlopen / the .NET runtime
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(${PROJECT_NAME} PRIVATE dl pthread)
endif()

# macOS needs CoreFoundation (hostfxr dependency)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_link_libraries(${PROJECT_NAME} PRIVATE "-framework CoreFoundation")
endif()

# -- C# bridge assembly --
set(BRIDGE_OUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/scripts/ScriptBridge")

# Write runtimeconfig.json for the bridge (class libs don't get one automatically)
file(WRITE "${BRIDGE_OUT}/ScriptBridge.runtimeconfig.json"
  [=[
{
  "runtimeOptions": {
    "tfm": "net10.0",
    "framework": {
      "name": "Microsoft.NETCore.App",
      "version": "10.0.0"
    },
    "configProperties": {
      "System.Runtime.Loader.UseRidGraph": true
    }
  }
}
]=]
)

add_custom_target(ScriptBridge ALL
  COMMAND ${CMAKE_COMMAND} -E env
  "DOTNET_BASE_INTERMEDIATE_OUTPUT_PATH=${CMAKE_BINARY_DIR}/ScriptBridge/obj/"
  ${DOTNET_EXE} publish
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ScriptBridge"
  -c Release
  -o "${BRIDGE_OUT}"
  -p:BuildRoot=${CMAKE_BINARY_DIR}/ScriptBridge/
  --nologo
  COMMENT "Building C# ScriptBridge"
  VERBATIM
)

add_dependencies(${PROJECT_NAME} ScriptBridge)

# Copy user scripts folder next to the executable
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/UserScripts"
  "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/scripts/UserScripts"
  COMMENT "Copying user scripts"
)

# Copy nethost.dll next to the exe (Windows only)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  find_file(NETHOST_DLL nethost.dll PATHS ${NETHOST_INCLUDE} NO_DEFAULT_PATH)
  if(NETHOST_DLL)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${NETHOST_DLL}"
      "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/nethost.dll"
      COMMENT "Copying nethost.dll"
    )
  endif()
endif()

# Print build output path
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo
  "-- Built: $<TARGET_FILE:${PROJECT_NAME}>"
  VERBATIM
)