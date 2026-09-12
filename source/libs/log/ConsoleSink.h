#ifndef CONSOLESINK_H
#define CONSOLESINK_H

#include "LogSink.h"

// Writes "[level][category] message" to stdout for trace/debug/info and stderr for warn/error, so
// output keeps appearing where it does today (the local server runs in its own console window).
class ConsoleSink : public LogSink {
public:
  void write(const LogEntry& entry) override;
};

#endif  // CONSOLESINK_H
