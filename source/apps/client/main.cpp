#include "ClientApp.h"
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
  try
  {
    // Defaults to singleplayer (spawn a local server). --host connects to an existing/remote server
    // instead (no local server spawned).
    ClientApp::ConnectOptions options { .launchLocalServer = true, .project = "SetupTest.json" };

    for (int i = 1; i < argc; ++i)
    {
      const std::string arg = argv[i];
      if (arg == "--host" && i + 1 < argc)
      {
        options.host = argv[++i];
        options.launchLocalServer = false;
      }
      else if (arg == "--port" && i + 1 < argc)
      {
        options.port = std::stoi(argv[++i]);
      }
      else if (arg == "--project" && i + 1 < argc)
      {
        options.project = argv[++i];
      }
    }

    ClientApp app(options);

    app.run();
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
