cmake_minimum_required(VERSION 3.29)

project(ECS3D)

# Enable Objective-C support on macOS
if(APPLE)
  enable_language(OBJC)
endif()

set(CMAKE_CXX_STANDARD 23)

if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

  # compile_commands.json, which is what gives clangd and the IDEs navigation across the whole tree, and
  # what clang-tidy would read if it is ever wired up. Only meaningful for the generators that emit it
  # (Ninja and Makefiles), and harmless elsewhere.
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

  include(CTest)
endif()

# The sanitize preset. Set at the top level and before anything is added, so every target the build
# produces carries the same flags - a sanitized binary linked against unsanitized objects reports
# nonsense, or fails to link at all.
option(ECS3D_SANITIZE "Build with AddressSanitizer, and UndefinedBehaviorSanitizer where available" OFF)

if (ECS3D_SANITIZE)
  if (MSVC)
    # MSVC ships ASan but no UBSan. Its ASan cannot coexist with the /RTC run-time checks CMake puts in
    # the Debug flags, nor with incremental linking, so both are cleared rather than left to fail later.
    string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

    add_compile_options(/fsanitize=address)
    add_link_options(/INCREMENTAL:NO)
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  else()
    message(WARNING "ECS3D_SANITIZE is on but ${CMAKE_CXX_COMPILER_ID} has no sanitizer flags here; building without them.")
  endif()
endif()

if (NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

if (MSVC)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS 1)
endif()

add_subdirectory(source)
