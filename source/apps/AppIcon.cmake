# Gives a Windows executable its icon. GLFW's Win32 backend loads the resource named GLFW_ICON for the
# window class, and Explorer shows the first icon resource, so one resource covers the executable file, the
# title bar and the taskbar. Inert on other platforms.
function(ecs3d_add_app_icon TARGET ICON_PATH)
  if(NOT WIN32)
    return()
  endif()

  if(NOT CMAKE_RC_COMPILER)
    message(WARNING "[AppIcon] No resource compiler; ${TARGET} will use the default executable icon.")
    return()
  endif()

  cmake_path(ABSOLUTE_PATH ICON_PATH BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE _ecs3d_icon)
  if(NOT EXISTS "${_ecs3d_icon}")
    message(FATAL_ERROR "[AppIcon] Icon for ${TARGET} not found: ${_ecs3d_icon}")
  endif()

  # file(GENERATE) writes UTF-8 and rc.exe defaults to the ANSI code page, so say so for non-ASCII paths.
  set(_ecs3d_rc "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}Icon.rc")
  file(GENERATE OUTPUT "${_ecs3d_rc}" CONTENT "#pragma code_page(65001)\nGLFW_ICON ICON \"${_ecs3d_icon}\"\n")

  # Not every generator scans the .rc for the files it references, so name the icon as a dependency.
  set_source_files_properties("${_ecs3d_rc}" PROPERTIES GENERATED TRUE OBJECT_DEPENDS "${_ecs3d_icon}")
  target_sources(${TARGET} PRIVATE "${_ecs3d_rc}")
endfunction()
