#include "EditorApp.h"
#include "ConsoleWindow.h"
#include <Log.h>
#include <ConsoleSink.h>
#include <memory>
#include <iostream>
#include <string>

int main(const int argc, char** argv)
{
  try
  {
    Log::addSink(std::make_shared<ConsoleSink>());

    // The editor is a GUI-subsystem build with no console by default; --console opens one.
    for (int i = 1; i < argc; ++i)
    {
      if (std::string(argv[i]) == "--console")
      {
        openConsoleWindow();
        break;
      }
    }

    // Defaults to spawning a local edit server. --host attaches to an existing server instead.
    EditorApp::LaunchOptions options;

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
      else if (arg == "--token" && i + 1 < argc)
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

    EditorApp app(options);

    app.run();
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::editor, e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
