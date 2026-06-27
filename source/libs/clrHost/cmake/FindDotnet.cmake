# cmake/FindDotnet.cmake
# Finds nethost + hostfxr for embedding .NET in a C++ app (Option A: require installed .NET).
# Sets after find_package(Dotnet REQUIRED):
#   DOTNET_EXE              - path to the dotnet CLI
#   DOTNET_RID              - runtime identifier (e.g. linux-x64, win-x64, osx-arm64)
#   DOTNET_RUNTIME_VERSION  - detected runtime version (e.g. 8.0.5)
#   NETHOST_INCLUDE         - directory containing nethost.h / hostfxr.h
#   NETHOST_LIB             - path to libnethost.a / nethost.lib

cmake_minimum_required(VERSION 3.20)

# -- 1. Find the dotnet CLI --
find_program(DOTNET_EXE dotnet
  HINTS
  "$ENV{DOTNET_ROOT}"
  "$ENV{HOME}/.dotnet"
  "/usr/local/share/dotnet"
  "/usr/share/dotnet"
  "/usr/lib/dotnet"
  "C:/Program Files/dotnet"
  "$ENV{ProgramFiles}/dotnet"
)

if(NOT DOTNET_EXE)
    message(FATAL_ERROR
      "\n[FindDotnet] dotnet CLI not found.\n"
      "Install the .NET SDK (>= 8) from: https://dotnet.microsoft.com/download\n"
      "Or set -DDOTNET_ROOT=<path> when running cmake.\n"
    )
endif()

# -- 2. Verify minimum version (>= 8) --
execute_process(
  COMMAND ${DOTNET_EXE} --version
  OUTPUT_VARIABLE DOTNET_VERSION_RAW
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE DOTNET_VERSION_RESULT
)
if(NOT DOTNET_VERSION_RESULT EQUAL 0)
    message(FATAL_ERROR
      "[FindDotnet] 'dotnet --version' failed. Is the .NET SDK installed correctly?\n"
      "Check: ${DOTNET_EXE} --version"
    )
endif()

string(REGEX MATCH "^([0-9]+)" DOTNET_MAJOR "${DOTNET_VERSION_RAW}")
if(DOTNET_MAJOR LESS 8)
    message(FATAL_ERROR
      "\n[FindDotnet] .NET ${DOTNET_VERSION_RAW} found, but >= 8.0 is required.\n"
      "Upgrade at: https://dotnet.microsoft.com/download\n"
    )
endif()
message(STATUS "[FindDotnet] Found .NET SDK ${DOTNET_VERSION_RAW}")

# -- 3. Detect Runtime Identifier (RID) --
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64")
        set(DOTNET_RID "win-arm64")
    else()
        set(DOTNET_RID "win-x64")
    endif()
    set(_nethost_libname "nethost.lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64"
      OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(DOTNET_RID "osx-arm64")
    else()
        set(DOTNET_RID "osx-x64")
    endif()
    set(_nethost_libname "libnethost.a")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
        set(DOTNET_RID "linux-arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm")
        set(DOTNET_RID "linux-arm")
    else()
        set(DOTNET_RID "linux-x64")
    endif()
    set(_nethost_libname "libnethost.a")
else()
    message(FATAL_ERROR "[FindDotnet] Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "[FindDotnet] Target RID: ${DOTNET_RID}")

# -- 4. Detect installed runtime version --
execute_process(
  COMMAND ${DOTNET_EXE} --list-runtimes
  OUTPUT_VARIABLE _runtimes_raw
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Pick the highest Microsoft.NETCore.App version >= 8 (list is sorted ascending)
set(DOTNET_RUNTIME_VERSION "")
string(REPLACE "\n" ";" _runtime_lines "${_runtimes_raw}")
foreach(_line IN LISTS _runtime_lines)
    if(_line MATCHES "Microsoft\\.NETCore\\.App ([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(_ver "${CMAKE_MATCH_1}")
        string(REGEX MATCH "^([0-9]+)" _major "${_ver}")
        if(_major GREATER_EQUAL 8)
            set(DOTNET_RUNTIME_VERSION "${_ver}")
        endif()
    endif()
endforeach()

if(NOT DOTNET_RUNTIME_VERSION)
    message(FATAL_ERROR
      "\n[FindDotnet] No Microsoft.NETCore.App runtime >= 8 found.\n"
      "Install the .NET Runtime from: https://dotnet.microsoft.com/download\n"
      "Output of 'dotnet --list-runtimes':\n${_runtimes_raw}\n"
    )
endif()
message(STATUS "[FindDotnet] Runtime version: ${DOTNET_RUNTIME_VERSION}")

# -- 5. Build candidate search paths --
set(_pack_rel "Microsoft.NETCore.App.Host.${DOTNET_RID}/${DOTNET_RUNTIME_VERSION}/runtimes/${DOTNET_RID}/native")

set(_dotnet_roots
  "$ENV{DOTNET_ROOT}"
  "$ENV{HOME}/.dotnet"
  "/usr/share/dotnet"
  "/usr/local/share/dotnet"
  "/usr/lib/dotnet"
  "C:/Program Files/dotnet"
  "$ENV{ProgramFiles}/dotnet"
)

set(_search_paths)
foreach(_root IN LISTS _dotnet_roots)
    list(APPEND _search_paths "${_root}/packs/${_pack_rel}")
endforeach()

find_path(NETHOST_INCLUDE nethost.h
  PATHS ${_search_paths}
  NO_DEFAULT_PATH
)
find_library(NETHOST_LIB
  NAMES ${_nethost_libname} nethost
  PATHS ${_search_paths}
  NO_DEFAULT_PATH
)

# -- 5b. Glob fallback for distro-specific RIDs (e.g. ubuntu.24.04-x64) --
# Some Linux distros install the host pack under a distro-specific RID
# rather than the generic linux-x64. Scan all Microsoft.NETCore.App.Host.*
# directories and look for a matching version.
if(NOT NETHOST_INCLUDE OR NOT NETHOST_LIB)
    foreach(_root IN LISTS _dotnet_roots)
        if(IS_DIRECTORY "${_root}/packs")
            file(GLOB _host_pack_dirs "${_root}/packs/Microsoft.NETCore.App.Host.*")
            foreach(_pack_dir IN LISTS _host_pack_dirs)
                set(_native_dir "${_pack_dir}/${DOTNET_RUNTIME_VERSION}/runtimes")
                if(IS_DIRECTORY "${_native_dir}")
                    file(GLOB _rid_dirs "${_native_dir}/*")
                    foreach(_rid_dir IN LISTS _rid_dirs)
                        set(_candidate "${_rid_dir}/native")
                        if(IS_DIRECTORY "${_candidate}")
                            find_path(NETHOST_INCLUDE nethost.h
                              PATHS "${_candidate}" NO_DEFAULT_PATH)
                            find_library(NETHOST_LIB
                              NAMES ${_nethost_libname} nethost
                              PATHS "${_candidate}" NO_DEFAULT_PATH)
                            if(NETHOST_INCLUDE AND NETHOST_LIB)
                                message(STATUS "[FindDotnet] Found host pack via distro RID: ${_candidate}")
                                break()
                            endif()
                        endif()
                    endforeach()
                endif()
                if(NETHOST_INCLUDE AND NETHOST_LIB)
                    break()
                endif()
            endforeach()
        endif()
        if(NETHOST_INCLUDE AND NETHOST_LIB)
            break()
        endif()
    endforeach()
endif()

# -- 6. Auto-restore via NuGet if still not found --
if(NOT NETHOST_INCLUDE OR NOT NETHOST_LIB)
    message(STATUS "[FindDotnet] Host pack not in SDK packs — attempting NuGet restore...")

    set(_probe "${CMAKE_BINARY_DIR}/_nethost_probe")
    file(MAKE_DIRECTORY "${_probe}")
    file(WRITE "${_probe}/probe.csproj"
      "<Project Sdk=\"Microsoft.NET.Sdk\">
            <PropertyGroup>
                <TargetFramework>net${DOTNET_MAJOR}.0</TargetFramework>
                <RuntimeIdentifier>${DOTNET_RID}</RuntimeIdentifier>
                <Nullable>enable</Nullable>
            </PropertyGroup>
            <ItemGroup>
                <PackageReference Include=\"Microsoft.NETCore.App.Host.${DOTNET_RID}\"
                                  Version=\"${DOTNET_RUNTIME_VERSION}\" />
            </ItemGroup>
        </Project>")

    execute_process(
      COMMAND ${DOTNET_EXE} restore "${_probe}/probe.csproj"
      --packages "${_probe}/packages"
      RESULT_VARIABLE _restore_result
      OUTPUT_QUIET
    )

    if(_restore_result EQUAL 0)
        string(TOLOWER "microsoft.netcore.app.host.${DOTNET_RID}" _pkg_lower)
        set(_nuget_native
          "${_probe}/packages/${_pkg_lower}/${DOTNET_RUNTIME_VERSION}/runtimes/${DOTNET_RID}/native"
        )
        find_path(NETHOST_INCLUDE nethost.h PATHS "${_nuget_native}" NO_DEFAULT_PATH)
        find_library(NETHOST_LIB
          NAMES ${_nethost_libname} nethost
          PATHS "${_nuget_native}"
          NO_DEFAULT_PATH
        )
    else()
        message(WARNING "[FindDotnet] NuGet restore failed.")
    endif()
endif()

# -- 7. Final check --
if(NOT NETHOST_INCLUDE OR NOT NETHOST_LIB)
    message(FATAL_ERROR
      "\n[FindDotnet] Could not locate nethost headers or library.\n"
      "  RID:     ${DOTNET_RID}\n"
      "  Runtime: ${DOTNET_RUNTIME_VERSION}\n\n"
      "Make sure the .NET SDK (not just Runtime) is installed:\n"
      "  https://dotnet.microsoft.com/download\n\n"
      "If the SDK is in a non-standard location, set:\n"
      "  -DDOTNET_ROOT=/path/to/dotnet\n"
    )
endif()

message(STATUS "[FindDotnet] nethost include: ${NETHOST_INCLUDE}")
message(STATUS "[FindDotnet] nethost lib:     ${NETHOST_LIB}")