project("ECS3DPhysicsBench")

# A headless console tool for repeatable physics timing: no CLR, scripts or networking, so it links only the
# data, sim and log libraries. Not built by default and not a CTest test; build it with
#   cmake --build <build-dir> --target ECS3DPhysicsBench
add_executable(${PROJECT_NAME} EXCLUDE_FROM_ALL
  main.cpp
  ../server/DefaultProject.cpp
  ../server/DefaultProject.h
)

# DefaultProject.cpp is plain JSON over json, glm and uuid, so it is compiled in directly (as the test suite
# does) rather than linking the server app.
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../server
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DData
  ECS3DSim
  ECS3DLog
)
