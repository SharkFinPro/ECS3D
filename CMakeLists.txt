cmake_minimum_required(VERSION 3.29)

project(ECS3D)

# Enable Objective-C support on macOS
if(APPLE)
  enable_language(OBJC)
endif()

set(CMAKE_CXX_STANDARD 23)

# C++23 with a 3.28+ minimum makes CMake scan every source for module imports before compiling it.
# Nothing here uses modules, and the scan cost about an eighth of a clean build's CPU time.
if (NOT DEFINED CMAKE_CXX_SCAN_FOR_MODULES)
  set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()

if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin)

  # Shared libraries are runtime output only on Windows. Elsewhere they use the library directory, and
  # dependencies such as assimp otherwise leave theirs in _deps, out of reach of the executables in bin.
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin)

  # Keeps bin runnable after it is copied elsewhere, since the absolute build RPATH would then point nowhere.
  if (UNIX AND NOT APPLE)
    list(APPEND CMAKE_BUILD_RPATH "$ORIGIN")
  endif()

  # compile_commands.json, which is what gives clangd and the IDEs navigation across the whole tree, and
  # what clang-tidy would read if it is ever wired up. Only meaningful for the generators that emit it
  # (Ninja and Makefiles), and harmless elsewhere. Not forced: a build that asked for it off keeps it off.
  # Those generators create the cache entry empty before this point, so empty counts as not asked.
  if ("${CMAKE_EXPORT_COMPILE_COMMANDS}" STREQUAL "")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  endif()

  include(CTest)

  # The sanitize preset. Whether a build is sanitized is a whole-build decision, so the option lives in
  # the top-level block and the flags are set before anything is added - every target the build produces
  # then carries them, including what FetchContent brings in. A sanitized binary linked against
  # unsanitized objects reports nonsense, or fails to link at all.
  option(ECS3D_SANITIZE "Build with AddressSanitizer, and UndefinedBehaviorSanitizer where available" OFF)

  if (ECS3D_SANITIZE)
    if (MSVC)
      # MSVC ships ASan but no UBSan. Its ASan cannot coexist with the /RTC run-time checks CMake puts
      # in the Debug flags, nor with incremental linking, so both are cleared rather than left to fail
      # later. The ASan runtime is a DLL here - see the README for why that matters at test time.
      string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
      string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

      add_compile_options(/fsanitize=address)
      add_link_options(/INCREMENTAL:NO)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      # -fno-sanitize-recover is what makes UBSan mean anything here: its default is to print and carry
      # on, so a suite run under the preset would report every finding and still exit 0.
      add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined
                          -fno-omit-frame-pointer)
      add_link_options(-fsanitize=address,undefined)
    else()
      message(WARNING "ECS3D_SANITIZE is on but ${CMAKE_CXX_COMPILER_ID} has no sanitizer flags here; building without them.")
    endif()
  endif()

  # The coverage preset. Like sanitizing, instrumenting is a whole-build decision, so the flags are set
  # here before anything is added. Every target the build produces is instrumented, though the check
  # target only builds, and so only measures, the test suite.
  option(ECS3D_COVERAGE "Build instrumented for line and branch coverage" OFF)

  if (ECS3D_COVERAGE)
    if (MSVC)
      # /PROFILE is what lets Microsoft.CodeCoverage.Console instrument the binaries; it needs no
      # compile flags. It implies /INCREMENTAL:NO, which the Debug link flags would otherwise contradict
      # (LNK4075).
      add_link_options(/PROFILE /INCREMENTAL:NO)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      # .gcno/.gcda output, which gcovr reads (Clang needs gcovr's --gcov-executable "llvm-cov gcov").
      # Atomic counter updates, since the suite runs OpenMP and test threads and the default
      # single-threaded updates leave inconsistent .gcda data that gcovr can abort on.
      add_compile_options(--coverage -fprofile-update=atomic)
      add_link_options(--coverage)
    else()
      message(WARNING "ECS3D_COVERAGE is on but ${CMAKE_CXX_COMPILER_ID} has no coverage flags here; building without them.")
    endif()
  endif()

  # The profile preset. Like the others it is a whole-build decision, so the flags are set before
  # anything is added and reach every target. RelWithDebInfo already brings optimization and debug info.
  option(ECS3D_PROFILE "Build optimized with frame pointers and linker settings that keep profiles readable" OFF)

  if (ECS3D_PROFILE)
    if (NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
      message(WARNING "ECS3D_PROFILE is meant for the RelWithDebInfo build type, not '${CMAKE_BUILD_TYPE}'.")
    endif()

    if (MSVC)
      # /DEBUG turns /OPT:REF off, leaving a larger and slower binary than Release, so REF is set back
      # on. /DEBUG already implies NOICF; it is stated to keep ICF off, since folding identical functions
      # makes a profiler attribute samples to the wrong one. /INCREMENTAL:NO conflicts with /OPT.
      add_link_options(/DEBUG /OPT:REF /OPT:NOICF /INCREMENTAL:NO)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      # Frame pointers let perf and other sampling profilers unwind without DWARF; -g comes from the
      # build type.
      add_compile_options(-fno-omit-frame-pointer)
    else()
      message(WARNING "ECS3D_PROFILE is on but ${CMAKE_CXX_COMPILER_ID} has no profiling flags here; building without them.")
    endif()
  endif()
endif()

if (NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

if (MSVC)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS 1)
endif()

add_subdirectory(source)
