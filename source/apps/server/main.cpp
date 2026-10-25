#include "ServerApp.h"
#include "ServerPolicy.h"
#include <ProjectPaths.h>
#include <EditToken.h>
#include <Log.h>
#include <ConsoleSink.h>
#include <LogSetup.h>
#include <memory>
#include <string>
#include <utility>

namespace
{
  bool isValueFlag(const std::string& arg, const char* flag, const int index, const int argc)
  {
    return arg == flag && index + 1 < argc;
  }

  // --edit is the launch-capability gate that allows editor connections; absent it the server is a
  // pure play server. --token, when set, is the secret an editor must present to be authorized;
  // main generates one for an --edit server given none. --open-edit implies --edit and skips that, so any
  // editor may edit (development only); an explicit --token still applies.
  // An empty project runs the built-in sample (Welcome and the demo scenes); --project loads a file.
  // --simulate-when-empty keeps the scene ticking with no one connected; by default it pauses until a join.
  ServerApp::LaunchOptions parseOptions(const int argc, char** argv)
  {
    ServerApp::LaunchOptions options;

    for (int i = 1; i < argc; ++i)
    {
      const std::string arg = argv[i];
      if (isValueFlag(arg, "--project", i, argc))
      {
        options.project = absoluteProjectArgument(argv[++i]);
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
      else if (arg == "--open-edit")
      {
        options.editMode = true;
        options.openEdit = true;
      }
      else if (arg == "--simulate-when-empty")
      {
        options.simulateWhenEmpty = true;
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

    auto options = parseOptions(argc, argv);

    if (needsOpenEditWarning(options.openEdit, options.authToken))
    {
      Log::warn(LogCategory::server, "--open-edit: any editor that can reach port " + std::to_string(options.port) +
                                       " may edit this server. Use it for development only.");
    }

    if (needsGeneratedEditToken(options.editMode, options.openEdit, options.authToken))
    {
      options.authToken = net::generateEditToken();
      Log::info(LogCategory::server, "No --token given; generated edit token " + options.authToken +
                                       ". Attach an editor with --host <host> --port " +
                                       std::to_string(options.port) + " --token " + options.authToken + ".");
    }

    ServerApp app(std::move(options));

    app.run();
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
