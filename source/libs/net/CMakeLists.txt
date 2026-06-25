project(ECS3DNet)

add_library(${PROJECT_NAME}
  Protocol.h
  MessageQueue.cpp
  MessageQueue.h
  NetServer.cpp
  NetServer.h
  NetClient.cpp
  NetClient.h
  ServerProcess.cpp
  ServerProcess.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  ECS3DData
  ECS3DClrHost
)

# The C# TCP transport assembly. The C++ side owns the protocol (opaque serialize() payloads); C#
# owns the socket. Published to <exe>/net/Transport; NetServer/NetClient load it through ManagedHost.
ecs3d_add_managed_assembly(${PROJECT_NAME}
  "${CMAKE_CURRENT_SOURCE_DIR}/Transport"
  "net/Transport"
  "ECS3DNetTransport"
)
