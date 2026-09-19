#include "ServerApp.h"
#include <Log.h>
#include <ConsoleSink.h>
#include <LogSetup.h>
#include <memory>
#include <string>

namespace
{
  bool isValueFlag(const std::string& arg, const char* flag, const int index, const int argc)
  {
    return arg == flag && index + 1 < argc;
  }

  // --edit is the launch-capability gate that allows editor connections; absent it the server is a
  // pure play server. --token, when set, is the secret an editor must present to be authorized.
  // An empty project runs the built-in sample (scenes 1-3 + falling balls); --project loads a file.
  ServerApp::LaunchOptions parseOptions(const int argc, char** argv)
  {
    ServerApp::LaunchOptions options;

    for (int i = 1; i < argc; ++i)
    {
      const std::string arg = argv[i];
      if (isValueFlag(arg, "--project", i, argc))
      {
        options.project = argv[++i];
      }
      else if (isValueFlag(arg, "--port", i, argc))
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
      else if (isValueFlag(arg, "--token", i, argc))
      {
        options.authToken = argv[++i];
      }
    }

    return options;
  }
}

int main(const int argc, char** argv)
{
  try
  {
    Log::addSink(std::make_shared<ConsoleSink>());

    addFileSinkFromArguments(argc, argv, "server", LogCategory::server);

    ServerApp app(parseOptions(argc, argv));

    app.run();
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
