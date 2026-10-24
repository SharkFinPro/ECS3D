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

  if(NOT EXISTS "${ICON_PATH}")
    message(FATAL_ERROR "[AppIcon] Icon for ${TARGET} not found: ${ICON_PATH}")
  endif()

  get_filename_component(_ecs3d_icon "${ICON_PATH}" ABSOLUTE)
  set(_ecs3d_rc "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}Icon.rc")
  file(GENERATE OUTPUT "${_ecs3d_rc}" CONTENT "GLFW_ICON ICON \"${_ecs3d_icon}\"\n")

  # Not every generator scans the .rc for the files it references, so name the icon as a dependency.
  set_source_files_properties("${_ecs3d_rc}" PROPERTIES GENERATED TRUE OBJECT_DEPENDS "${_ecs3d_icon}")
  target_sources(${TARGET} PRIVATE "${_ecs3d_rc}")
endfunction()
