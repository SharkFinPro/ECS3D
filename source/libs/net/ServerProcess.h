#ifndef SERVERPROCESS_H
#define SERVERPROCESS_H

#include <string>

namespace net {

// Launches a child ECS3DServer process for singleplayer / local-MP (the editor/client spawn a local
// server and connect over loopback - the same netcode path as remote). RAII: the child is terminated
// when this object is destroyed, so the local server doesn't outlive its window.
class ServerProcess {
public:
  ServerProcess() = default;
  ServerProcess(const ServerProcess&) = delete;
  ServerProcess& operator=(const ServerProcess&) = delete;

  ~ServerProcess();

  // Launch <directory of the current executable>/<exeBaseName>, with its working directory set to that
  // same directory so it resolves net/Transport / scripts / assets the way the headless server does.
  // `arguments` (if any) are appended to the command line. Returns true if the process started.
  bool launch(const std::string& exeBaseName, const std::string& arguments = "");

private:
  void* m_handle = nullptr;
};

}



#endif //SERVERPROCESS_H
