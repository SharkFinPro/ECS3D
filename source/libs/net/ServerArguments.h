#ifndef SERVERARGUMENTS_H
#define SERVERARGUMENTS_H

#include <string>

namespace net {

// The --port flag (with its leading space) for a spawned local server, so the server listens on the port its
// parent connects to.
[[nodiscard]] inline std::string serverPortArgument(const int port)
{
  return " --port " + std::to_string(port);
}

}

#endif //SERVERARGUMENTS_H
