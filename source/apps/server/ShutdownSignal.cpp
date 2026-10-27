#include "ShutdownSignal.h"
#include <atomic>
#include <csignal>

namespace
{
  std::atomic<bool> s_shutdownRequested{false};

  static_assert(std::atomic<bool>::is_always_lock_free, "the flag is written from a signal handler");

  extern "C" void handleShutdownSignal(const int signalNumber)
  {
    s_shutdownRequested.store(true);
    std::signal(signalNumber, SIG_DFL);
  }
}

void installShutdownSignalHandlers()
{
  std::signal(SIGINT, handleShutdownSignal);
  std::signal(SIGTERM, handleShutdownSignal);
#if defined(SIGBREAK)
  std::signal(SIGBREAK, handleShutdownSignal);
#endif
}

bool shutdownRequested() noexcept
{
  return s_shutdownRequested.load();
}

void clearShutdownRequest() noexcept
{
  s_shutdownRequested.store(false);
}
