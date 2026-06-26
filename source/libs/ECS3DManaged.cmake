# ecs3d_add_managed_assembly(<native_target> <csproj_dir> <out_subdir> <assembly_name>)
#
# Publishes a C# class library next to the runtime output and makes <native_target> depend
# on it. Each module that ships an assembly calls this for its own .csproj:
#   ECS3DScripting -> ScriptBridge (gameplay), ECS3DNet -> the websocket transport assembly.
#
# The "host linkage" half (nethost include/link) lives in ECS3DClrHost and is inherited transitively.
function(ecs3d_add_managed_assembly NATIVE_TARGET CSPROJ_DIR OUT_SUBDIR ASSEMBLY_NAME)
  find_package(Dotnet REQUIRED)

  set(ASM_OUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${OUT_SUBDIR}")

  # Write runtimeconfig.json for the assembly (class libs don't get one automatically)
  file(WRITE "${ASM_OUT}/${ASSEMBLY_NAME}.runtimeconfig.json"
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
    "${CSPROJ_DIR}"
    -c Release
    -o "${ASM_OUT}"
    -p:BuildRoot=${CMAKE_BINARY_DIR}/${ASSEMBLY_NAME}/
    --nologo
    COMMENT "Building C# ${ASSEMBLY_NAME}"
    VERBATIM
  )

  add_dependencies(${NATIVE_TARGET} ${ASSEMBLY_NAME})
endfunction()
