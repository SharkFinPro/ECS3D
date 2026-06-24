project("ECS3DClient")

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE ECS3DCore)

target_include_directories(${PROJECT_NAME} PRIVATE
  ../../core
)