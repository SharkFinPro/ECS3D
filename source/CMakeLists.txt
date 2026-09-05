add_subdirectory(libs)
add_subdirectory(apps)

if (BUILD_TESTING)
  add_subdirectory(tests)
endif()
