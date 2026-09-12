#include "ConsoleSink.h"
#include <iostream>
#include <ostream>
#include <string_view>

void ConsoleSink::write(const LogEntry& entry)
{
  std::ostream& out = entry.level >= LogLevel::warn ? std::cerr : std::cout;

  out << "[" << toString(entry.level) << "][" << toString(entry.category) << "] " << entry.message
      << std::endl;
}
