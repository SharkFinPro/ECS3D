#ifndef LOGSINK_H
#define LOGSINK_H

#include "LogEntry.h"

// A destination for log entries. A sink must not log from inside write() - Log::write calls sinks
// while holding its own lock, so a reentrant call would deadlock.
class LogSink {
public:
  virtual ~LogSink() = default;

  virtual void write(const LogEntry& entry) = 0;
};

#endif  // LOGSINK_H
