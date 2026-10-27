#ifndef CHILDPROCESS_H
#define CHILDPROCESS_H

#if !defined(_WIN32)

#include <chrono>
#include <sys/types.h>

namespace net {

// Asks the child to exit with SIGTERM and reaps it. A child still running when the grace expires is killed with
// SIGKILL, so a child that hangs while shutting down cannot hang its parent. Returns the wait status.
int terminateChildProcess(pid_t pid, std::chrono::milliseconds grace);

}

#endif

#endif //CHILDPROCESS_H
