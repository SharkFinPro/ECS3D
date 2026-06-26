#include "EditorApp.h"
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
  try
  {
    // Defaults to spawning a local edit server. --host attaches to an existing server instead.
    EditorApp::LaunchOptions options { .project = "SetupTest.json" };

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
    }

    EditorApp app(options);

    app.run();
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
