# ECS3D Launcher — a standalone Avalonia (C#) GUI app.
#
# Unlike the other apps this does NOT boot the CLR through ECS3DClrHost. It is a self-standing .NET
# process that shares the machine's installed .NET 10 runtime (framework-dependent). CMake drives
# `dotnet publish` at build time and drops a single-file ECS3DLauncher.exe directly in bin/ (beside
# the C++ executables); nothing lands in the source tree.
#
# ECS3DLauncher IS a real add_executable target (not a custom target) on purpose: CLion only creates
# Run/Debug configurations for genuine executable targets — a custom target yields "Error: Executable
# is not specified". So we compile a trivial stub into bin/ECS3DLauncher.exe to give CMake/CLion a
# first-class runnable target, then overwrite that stub in-place with the real .NET single-file app in
# a POST_BUILD step. The C language is enabled just for that one-line stub.
#
# It is otherwise self-sufficient: the FindDotnet module and ecs3d_add_managed_assembly helper live at
# the source/libs scope and are not visible here under source/apps. All it needs is the dotnet CLI.
project(ECS3DLauncher C)

# Reuse the cached DOTNET_EXE from the libs-side FindDotnet if it already ran; otherwise locate it.
if(NOT DOTNET_EXE)
  find_program(DOTNET_EXE dotnet REQUIRED
    HINTS
      "$ENV{DOTNET_ROOT}"
      "$ENV{HOME}/.dotnet"
      "/usr/local/share/dotnet"
      "/usr/share/dotnet"
      "/usr/lib/dotnet"
      "C:/Program Files/dotnet"
      "$ENV{ProgramFiles}/dotnet"
  )
endif()

# Framework-dependent, but still per-RID: Avalonia ships native graphics libs (Skia/HarfBuzz) for
# every platform, so a RID-less publish would copy them all (~500 MB). Pin to this build's RID so
# only the current platform's natives are emitted. Mirrors FindDotnet.cmake's RID detection.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64")
    set(_launcher_rid win-arm64)
  else()
    set(_launcher_rid win-x64)
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(_launcher_rid osx-arm64)
  else()
    set(_launcher_rid osx-x64)
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
    set(_launcher_rid linux-arm64)
  else()
    set(_launcher_rid linux-x64)
  endif()
else()
  message(FATAL_ERROR "[ECS3DLauncher] Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# The runnable ECS3DLauncher target. A trivial stub is compiled to bin/ECS3DLauncher.exe so CMake/CLion
# see a real executable; the POST_BUILD below replaces it with the real .NET app. OUTPUT_NAME/dir are
# forced so the linked exe lands exactly where `dotnet publish -o` will overwrite it.
set(_stub "${CMAKE_CURRENT_BINARY_DIR}/launcher_apphost_stub.c")
file(WRITE "${_stub}" "/* Placeholder apphost — overwritten in-place by the dotnet single-file publish\n   (see ECS3DLauncher/CMakeLists.txt). Exists only so CLion has a runnable target. */\nint main(void) { return 0; }\n")

add_executable(${PROJECT_NAME} "${_stub}")
set_target_properties(${PROJECT_NAME} PROPERTIES
  OUTPUT_NAME ECS3DLauncher
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# Relink the stub — and therefore re-run the publish below — whenever any C#/XAML/project file changes,
# so a CMake build never leaves a stale launcher. (Unchanged sources → no relink → publish skipped.)
file(GLOB_RECURSE _launcher_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/source/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.csproj"
  "${CMAKE_CURRENT_SOURCE_DIR}/Directory.Build.props")
set_source_files_properties("${_stub}" PROPERTIES OBJECT_DEPENDS "${_launcher_sources}")

# Overwrite the stub exe with the real single-file .NET app. PublishSingleFile +
# IncludeNativeLibrariesForSelfExtract bundle the ~35 Avalonia/Skia managed + native libs into the one
# apphost, so bin/ gets just ECS3DLauncher.exe, not 36 loose files. Framework-dependent (shares the
# engine's .NET 10). obj/bin intermediates go into the CMake tree via LauncherObjDir/LauncherBinDir
# (see Directory.Build.props) so the source dir stays untouched. Always Release: the csproj's
# Debug-only dev tooling (HotAvalonia/DevTools) must never ship. Finally, delete the stub's MSVC
# pdb/ilk so bin/ carries only the exe.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${DOTNET_EXE} publish
    "${CMAKE_CURRENT_SOURCE_DIR}/ECS3DLauncher.csproj"
    -c Release
    -r ${_launcher_rid}
    --self-contained false
    -p:PublishSingleFile=true
    -p:IncludeNativeLibrariesForSelfExtract=true
    -p:DebugType=none
    -p:DebugSymbols=false
    -o "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    -p:LauncherObjDir=${CMAKE_BINARY_DIR}/${PROJECT_NAME}/obj/
    -p:LauncherBinDir=${CMAKE_BINARY_DIR}/${PROJECT_NAME}/bin/
    --nologo
  COMMAND ${CMAKE_COMMAND} -E rm -f
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ECS3DLauncher.pdb"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ECS3DLauncher.ilk"
  COMMENT "Publishing C# ${PROJECT_NAME} (${_launcher_rid}) single-file -> bin/ECS3DLauncher.exe"
  VERBATIM
)
