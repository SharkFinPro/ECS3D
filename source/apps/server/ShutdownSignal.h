#ifndef SHUTDOWNSIGNAL_H
#define SHUTDOWNSIGNAL_H

// The first SIGINT, SIGTERM or (Windows) SIGBREAK sets a flag that ServerApp::run polls, so the server
// stops in order. The handler then restores the default action, so a second signal ends the process at once.
void installShutdownSignalHandlers();

[[nodiscard]] bool shutdownRequested() noexcept;

void clearShutdownRequest() noexcept;

#endif //SHUTDOWNSIGNAL_H
