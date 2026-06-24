#include "ServerApp.h"
#include <iostream>

int main(int argc, char** argv)
{
  try
  {
    // TODO: parse argv for --project, --port, --edit, --token (the ServerLaunch options). --edit
    // TODO:   is the launch-capability gate that allows editor connections; absent it, the server
    // TODO:   is a pure play server. The server must still work as a standalone dedicated app.
    (void)argc;
    (void)argv;

    ServerApp app({ .project = "SetupTest.json" });

    app.run();
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
