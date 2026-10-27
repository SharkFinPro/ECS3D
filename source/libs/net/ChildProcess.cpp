#include "ChildProcess.h"

#if !defined(_WIN32)

#include <cerrno>
#include <csignal>
#include <thread>
#include <sys/wait.h>

namespace net {

int terminateChildProcess(const pid_t pid, const std::chrono::milliseconds grace)
{
  kill(pid, SIGTERM);

  const auto deadline = std::chrono::steady_clock::now() + grace;
  int status = 0;

  while (std::chrono::steady_clock::now() < deadline)
  {
    const pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid)
    {
      return status;
    }

    if (result < 0 && errno != EINTR)
    {
      return status;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  kill(pid, SIGKILL);

  while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
  {
  }

  return status;
}

}

#endif
