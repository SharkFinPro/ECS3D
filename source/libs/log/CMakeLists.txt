project(ECS3DLog)

add_library(${PROJECT_NAME}
  Log.cpp
  Log.h
  LogEntry.cpp
  LogEntry.h
  LogSink.h
  ConsoleSink.cpp
  ConsoleSink.h
  ConsoleWindow.cpp
  ConsoleWindow.h
  FileSink.cpp
  FileSink.h
  RingBufferSink.cpp
  RingBufferSink.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
