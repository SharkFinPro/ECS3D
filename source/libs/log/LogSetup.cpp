#include "LogSetup.h"
#include "Log.h"
#include "UserDataDirectory.h"
#include <filesystem>
#include <string>
#include <system_error>

std::shared_ptr<FileSink> addFileSinkFromArguments(const int argc, char** argv,
                                                   const std::string_view appName,
                                                   const LogCategory category)
{
  try
  {
    std::filesystem::path file;
    bool disabled = false;

    for (int i = 1; i < argc; ++i)
    {
      const std::string arg = argv[i];
      if (arg == "--no-log-file")
      {
        disabled = true;
      }
      // A trailing --log-file with nothing after it is treated as absent rather than as an empty path.
      else if (arg == "--log-file" && i + 1 < argc)
      {
        file = argv[++i];
      }
    }

    if (disabled)
    {
      return nullptr;
    }

    if (file.empty())
    {
      file = defaultLogFile(appName);
    }

    auto sink = std::make_shared<FileSink>(file);

    if (!sink->isOpen())
    {
      Log::warn(category, "Could not open log file " + file.string());
      return nullptr;
    }

    Log::addSink(sink);
    Log::info(category, "Logging to " + file.string());

    return sink;
  }
  catch (...)
  {
    return nullptr;
  }
}

std::string logFileArgument(const std::string_view appName)
{
  std::error_code error;
  auto file = std::filesystem::absolute(defaultLogFile(appName), error);
  if (error)
  {
    file = defaultLogFile(appName);
  }

  const auto path = file.string();

  if (path.find('"') != std::string::npos)
  {
    return {};
  }

  return "--log-file \"" + path + "\"";
}
