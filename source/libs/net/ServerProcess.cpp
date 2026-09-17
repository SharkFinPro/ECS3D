#include "ServerProcess.h"
#include <filesystem>
#include <string>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#else
#include <climits>
#include <csignal>
#include <cstdint>
#include <fcntl.h>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

#if defined(_WIN32)
namespace {
  // The arguments can carry a file system path (the spawned server's log file), so the command line is
  // converted through the ANSI code page rather than widened byte by byte: that alone would mangle a
  // user name the code page spells with more than one byte. A name the code page cannot represent at
  // all is already lost before this, when the path is narrowed to build the argument string.
  std::wstring widen(const std::string& text)
  {
    if (text.empty())
    {
      return {};
    }

    const int length = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0);
    if (length <= 0)
    {
      return {};
    }

    std::wstring wide;
    wide.resize(static_cast<size_t>(length));
    MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);

    return wide;
  }
}
#else
namespace {
  // The arguments arrive as one command line and execv wants them split. Double quotes group a token so
  // an argument can hold a path with spaces, which the caller relies on: the per-user directory the
  // spawned server logs into contains one on macOS. Windows leaves this to the child's own CRT parser.
  std::vector<std::string> splitArguments(const std::string& arguments)
  {
    std::vector<std::string> tokens;

    std::string token;
    bool quoted = false;
    bool started = false;

    for (const char character : arguments)
    {
      if (character == '"')
      {
        quoted = !quoted;
        started = true;
      }
      else if (!quoted && (character == ' ' || character == '\t'))
      {
        if (started)
        {
          tokens.push_back(token);
          token.clear();
          started = false;
        }
      }
      else
      {
        token.push_back(character);
        started = true;
      }
    }

    if (started)
    {
      tokens.push_back(token);
    }

    return tokens;
  }
}
#endif

namespace net {

ServerProcess::~ServerProcess()
{
#if defined(_WIN32)
  if (m_handle)
  {
    TerminateProcess(static_cast<HANDLE>(m_handle), 0);
    CloseHandle(static_cast<HANDLE>(m_handle));
    m_handle = nullptr;
  }
#else
  if (m_handle)
  {
    const pid_t pid = static_cast<pid_t>(reinterpret_cast<intptr_t>(m_handle));
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    m_handle = nullptr;
  }
#endif
}

bool ServerProcess::launch(const std::string& exeBaseName, const std::string& arguments, const bool showConsole)
{
#if defined(_WIN32)
  if (m_handle)
  {
    return true;
  }

  // Resolve the server next to the running executable so it works regardless of the working directory.
  wchar_t modulePath[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, modulePath, MAX_PATH);

  const auto exeDir = std::filesystem::path(modulePath).parent_path();
  const auto serverExe = exeDir / (exeBaseName + ".exe");

  if (!std::filesystem::exists(serverExe))
  {
    return false;
  }

  const std::wstring applicationName = serverExe.wstring();
  const std::wstring workingDir = exeDir.wstring();

  // CreateProcessW needs a writable command-line buffer.
  std::wstring commandLine = L"\"" + applicationName + L"\"";
  if (!arguments.empty())
  {
    commandLine += L" " + widen(arguments);
  }
  std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
  commandBuffer.push_back(L'\0');

  STARTUPINFOW startupInfo {};
  startupInfo.cb = sizeof(startupInfo);
  PROCESS_INFORMATION processInfo {};

  // CREATE_NEW_CONSOLE so the local server's log (heartbeat, snapshot, script output) is visible in its
  // own window during singleplayer rather than being swallowed; CREATE_NO_WINDOW instead hides it entirely.
  const DWORD creationFlags = showConsole ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW;
  if (!CreateProcessW(applicationName.c_str(), commandBuffer.data(), nullptr, nullptr, FALSE,
                      creationFlags, nullptr, workingDir.c_str(), &startupInfo, &processInfo))
  {
    return false;
  }

  CloseHandle(processInfo.hThread);
  m_handle = processInfo.hProcess;

  return true;
#else
  if (m_handle)
  {
    return true;
  }

  // Resolve the server next to the running executable so it works regardless of the working directory.
  char modulePath[PATH_MAX] = {};
#if defined(__APPLE__)
  uint32_t modulePathSize = sizeof(modulePath);
  if (_NSGetExecutablePath(modulePath, &modulePathSize) != 0)
  {
    return false;
  }
#else
  const ssize_t modulePathLen = readlink("/proc/self/exe", modulePath, sizeof(modulePath) - 1);
  if (modulePathLen <= 0)
  {
    return false;
  }
  modulePath[modulePathLen] = '\0';
#endif

  const auto exeDir = std::filesystem::path(modulePath).parent_path();
  const auto serverExe = exeDir / exeBaseName;

  if (!std::filesystem::exists(serverExe))
  {
    return false;
  }

  const std::string serverExePath = serverExe.string();
  const std::string workingDir = exeDir.string();

  // Build argv: the server path followed by the launch flags. execv wants a null-terminated array of
  // writable C strings, so keep the backing strings alive in argStorage.
  std::vector<std::string> argStorage { serverExePath };
  for (auto& token : splitArguments(arguments))
  {
    argStorage.push_back(std::move(token));
  }

  std::vector<char*> argv;
  argv.reserve(argStorage.size() + 1);
  for (std::string& arg : argStorage)
  {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  const pid_t pid = fork();
  if (pid < 0)
  {
    return false;
  }

  if (pid == 0)
  {
    // Child: only async-signal-safe calls are allowed here. When hidden, redirect stdout/stderr to
    // /dev/null before touching anything else, then match the headless server's working directory and
    // replace the image. execv only returns on failure, so any path past it is an error.
    if (!showConsole)
    {
      const int devNull = open("/dev/null", O_WRONLY);
      if (devNull >= 0)
      {
        dup2(devNull, STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO);
        if (devNull > STDERR_FILENO)
        {
          close(devNull);
        }
      }
    }
    if (chdir(workingDir.c_str()) == 0)
    {
      execv(serverExePath.c_str(), argv.data());
    }
    _exit(127);
  }

  m_handle = reinterpret_cast<void*>(static_cast<intptr_t>(pid));
  return true;
#endif
}

}
