#include "ClientApp.h"
#include "ConsoleWindow.h"
#include <FramePacingSettings.h>
#include <ProjectPaths.h>
#include <Log.h>
#include <ConsoleSink.h>
#include <LogSetup.h>
#include <memory>
#include <stdexcept>
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

  int parseFpsLimit(const char* text)
  {
    const auto parsed = text ? framePacingSettings::parseMaxFps(text) : std::nullopt;
    if (!parsed)
    {
      throw std::invalid_argument(std::string("--fps-limit needs a whole number of frames per second (0 for unlimited, "
        "otherwise clamped to ") + std::to_string(framePacingSettings::minMaxFps) + "-"
        + std::to_string(framePacingSettings::maxMaxFps) + "), got " + (text ? "\"" + std::string(text) + "\"" : "nothing"));
    }

    return *parsed;
  }

  // Defaults to singleplayer (spawn a local server). --host connects to an existing/remote server
  // instead (no local server spawned).
  ClientApp::ConnectOptions parseOptions(const int argc, char** argv)
  {
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
        options.project = absoluteProjectArgument(argv[++i]);
      }
      else if (arg == "--stats")
      {
        options.showStats = true;
      }
      else if (arg == "--windowed")
      {
        options.windowed = true;
      }
      else if (arg == "--server-console")
      {
        options.showServerConsole = true;
      }
      else if (arg == "--vsync")
      {
        options.vsync = true;
      }
      else if (arg == "--no-vsync")
      {
        options.vsync = false;
      }
      else if (arg == "--fps-limit")
      {
        options.maxFps = parseFpsLimit(i + 1 < argc ? argv[++i] : nullptr);
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

    // The client is a GUI-subsystem build with no console by default; --console opens one.
    if (hasConsoleFlag(argc, argv))
    {
      openConsoleWindow();
    }

    addFileSinkFromArguments(argc, argv, "client", LogCategory::client);

    ClientApp app(parseOptions(argc, argv));

    app.run();
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::client, e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
