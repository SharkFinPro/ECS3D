project(ECS3DSettings)

add_library(${PROJECT_NAME}
  SettingsStore.cpp
  SettingsStore.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  nlohmann_json::nlohmann_json
)
