add_subdirectory(libs)
add_subdirectory(apps)

if (PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  add_subdirectory(tests)
endif()
