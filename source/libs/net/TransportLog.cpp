#include "TransportLog.h"
#include <Log.h>

namespace net {

namespace {
  // The cast below assumes LogLevel's underlying values are exactly 0..4 in trace..error order.
  static_assert(static_cast<int>(LogLevel::trace) == 0 && static_cast<int>(LogLevel::debug) == 1 &&
                static_cast<int>(LogLevel::info) == 2 && static_cast<int>(LogLevel::warn) == 3 &&
                static_cast<int>(LogLevel::error) == 4);
}

void transportLog(const int level, const char* message)
{
  if (message == nullptr)
  {
    return;
  }

  try
  {
    const auto mapped = level >= 0 && level <= 4 ? static_cast<LogLevel>(level) : LogLevel::info;
    Log::write(mapped, LogCategory::net, message);
  }
  catch (...)
  {
    // Called from a C# socket thread across an unmanaged boundary; nothing may escape.
  }
}

}
