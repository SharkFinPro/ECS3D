#include "EditorApp.h"
#include "ConsoleWindow.h"
#include <Log.h>
#include <ConsoleSink.h>
#include <LogSetup.h>
#include <memory>
#include <iostream>
#include <string>

namespace
{
  bool hasConsoleFlag(const int argc, char** argv)
  {
    for (int i = 1; i < argc; ++i)
    {
      if (std::string(argv[i]) == "--console")
      {
        return true;
      }
    }

    return false;
  }

  bool isValueFlag(const std::string& arg, const char* flag, const int index, const int argc)
  {
    return arg == flag && index + 1 < argc;
  }

  // Defaults to spawning a local edit server. --host attaches to an existing server instead.
  EditorApp::LaunchOptions parseOptions(const int argc, char** argv)
  {
    EditorApp::LaunchOptions options;

    for (int i = 1; i < argc; ++i)
    {
      const std::string arg = argv[i];
      if (isValueFlag(arg, "--host", i, argc))
      {
        options.host = argv[++i];
        options.launchLocalServer = false;
      }
      else if (isValueFlag(arg, "--port", i, argc))
      {
        options.port = std::stoi(argv[++i]);
      }
      else if (isValueFlag(arg, "--project", i, argc))
      {
        options.project = argv[++i];
      }
      else if (isValueFlag(arg, "--token", i, argc))
      {
        // The edit token to present when attaching to an existing edit server (--host); a spawned local
        // server gets its own generated token instead.
        options.authToken = argv[++i];
      }
      else if (arg == "--no-server-console")
      {
        options.showServerConsole = false;
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

    // The editor is a GUI-subsystem build with no console by default; --console opens one.
    if (hasConsoleFlag(argc, argv))
    {
      openConsoleWindow();
    }

    addFileSinkFromArguments(argc, argv, "editor", LogCategory::editor);

    EditorApp app(parseOptions(argc, argv));

    app.run();
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::editor, e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
