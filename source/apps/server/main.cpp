#include "ServerApp.h"
#include <Log.h>
#include <ConsoleSink.h>
#include <memory>
#include <string>

int main(const int argc, char** argv)
{
  Log::addSink(std::make_shared<ConsoleSink>());

  try
  {
    // --edit is the launch-capability gate that allows editor connections; absent it the server is a
    // pure play server. --token, when set, is the secret an editor must present to be authorized.
    // An empty project runs the built-in sample (scenes 1-3 + falling balls); --project loads a file.
    ServerApp::LaunchOptions options;

    for (int i = 1; i < argc; ++i)
    {
      const std::string arg = argv[i];
      if (arg == "--project" && i + 1 < argc)
      {
        options.project = argv[++i];
      }
      else if (arg == "--port" && i + 1 < argc)
      {
        options.port = std::stoi(argv[++i]);
      }
      else if (arg == "--edit")
      {
        options.editMode = true;
      }
      else if (arg == "--ephemeral")
      {
        // An editor/client-spawned local server: exit once its last connection drops, rather than
        // running until killed like a dedicated server.
        options.exitWhenEmpty = true;
      }
      else if (arg == "--token" && i + 1 < argc)
      {
        options.authToken = argv[++i];
      }
    }

    ServerApp app(options);

    app.run();
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
