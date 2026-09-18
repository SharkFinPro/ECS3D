#include "ClientApp.h"
#include "ConsoleWindow.h"
#include <Log.h>
#include <ConsoleSink.h>
#include <LogSetup.h>
#include <memory>
#include <iostream>
#include <string>

int main(const int argc, char** argv)
{
  try
  {
    Log::addSink(std::make_shared<ConsoleSink>());

    // The client is a GUI-subsystem build with no console by default; --console opens one.
    for (int i = 1; i < argc; ++i)
    {
      if (std::string(argv[i]) == "--console")
      {
        openConsoleWindow();
        break;
      }
    }

    addFileSinkFromArguments(argc, argv, "client", LogCategory::client);

    // Defaults to singleplayer (spawn a local server). --host connects to an existing/remote server
    // instead (no local server spawned).
    ClientApp::ConnectOptions options { .launchLocalServer = true };

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
      else if (arg == "--no-server-console")
      {
        options.showServerConsole = false;
      }
    }

    ClientApp app(options);

    app.run();
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::client, e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
