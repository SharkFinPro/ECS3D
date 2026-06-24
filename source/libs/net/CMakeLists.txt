project(ECS3DNet)

add_library(${PROJECT_NAME}
  Protocol.h
  MessageQueue.cpp
  MessageQueue.h
  NetServer.cpp
  NetServer.h
  NetClient.cpp
  NetClient.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  ECS3DData
  ECS3DClrHost
)

# TODO: add the C# websocket transport assembly (its own .csproj) and build it via the helper:
# TODO:   ecs3d_add_managed_assembly(${PROJECT_NAME}
# TODO:     "${CMAKE_CURRENT_SOURCE_DIR}/Transport" "net/Transport" "ECS3DNetTransport")
# TODO:   The C++ side owns the protocol (opaque serialize() payloads); C# owns the socket.
