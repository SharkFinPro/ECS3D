#ifndef FILESINK_H
#define FILESINK_H

#include "LogSink.h"
#include <filesystem>
#include <fstream>
#include <mutex>

// Writes "<UTC timestamp> [level][category] message" lines to a file. Thread-safe.
class FileSink : public LogSink {
public:
  // Creates missing parent directories and truncates any existing file, so each run starts a fresh log.
  // Never throws: if the file cannot be opened, isOpen() is false and write() does nothing.
  explicit FileSink(const std::filesystem::path& file);

  void write(const LogEntry& entry) override;

  [[nodiscard]] bool isOpen() const;

private:
  mutable std::mutex m_mutex;
  std::ofstream m_stream;
};

#endif  // FILESINK_H
