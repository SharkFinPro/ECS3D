# Helpers for libraries/apps that ship or host C# assemblies. Mirrors the publish step that used to
# live in core/cmake/loadCS.cmake, but factored so each lib (ECS3DScripting, ECS3DNet) declares its
# own assembly and each executable deploys the native runtime shim.

# Build + publish a C# class library next to the executables and emit its runtimeconfig.json (class
# libs don't get one automatically, and hostfxr needs one to boot). TARGET is the C++ library that
# owns the assembly; the publish runs at build time as a dependency of TARGET.
function(ecs3d_add_managed_assembly TARGET SRC_DIR OUT_SUBDIR ASSEMBLY_NAME)
  find_package(Dotnet REQUIRED)

  set(_out "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${OUT_SUBDIR}")

  file(WRITE "${_out}/${ASSEMBLY_NAME}.runtimeconfig.json"
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

  add_custom_target(${ASSEMBLY_NAME} ALL
    COMMAND ${CMAKE_COMMAND} -E env
      "DOTNET_BASE_INTERMEDIATE_OUTPUT_PATH=${CMAKE_BINARY_DIR}/${ASSEMBLY_NAME}/obj/"
      ${DOTNET_EXE} publish
      "${SRC_DIR}"
      -c Release
      -o "${_out}"
      -p:BuildRoot=${CMAKE_BINARY_DIR}/${ASSEMBLY_NAME}/
      --nologo
    COMMENT "Building C# ${ASSEMBLY_NAME}"
    VERBATIM
  )

  add_dependencies(${TARGET} ${ASSEMBLY_NAME})
endfunction()

# Copy nethost.dll next to an executable. Every app boots the runtime (they all link ECS3DClrHost via
# ECS3DNet), and a static lib has no runtime output of its own, so each exe deploys the shim itself.
function(ecs3d_deploy_clr_runtime TARGET)
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    find_file(NETHOST_DLL nethost.dll PATHS ${NETHOST_INCLUDE} NO_DEFAULT_PATH)
    if(NETHOST_DLL)
      add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${NETHOST_DLL}" "$<TARGET_FILE_DIR:${TARGET}>/nethost.dll"
        COMMENT "Deploying nethost.dll for ${TARGET}"
        VERBATIM
      )
    else()
      message(WARNING "[ECS3DManaged] nethost.dll not found near ${NETHOST_INCLUDE}; ${TARGET} will "
                      "not be able to boot the CLR until it is copied next to the executable.")
    endif()
  endif()
endfunction()
