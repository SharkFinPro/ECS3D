#include "ServerProcess.h"
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#else
#include <climits>
#include <csignal>
#include <cstdint>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
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

bool ServerProcess::launch(const std::string& exeBaseName, const std::string& arguments)
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
    // Arguments are ASCII launch flags (e.g. "--ephemeral"), so a plain widening is sufficient.
    commandLine += L" " + std::wstring(arguments.begin(), arguments.end());
  }
  std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
  commandBuffer.push_back(L'\0');

  STARTUPINFOW startupInfo {};
  startupInfo.cb = sizeof(startupInfo);
  PROCESS_INFORMATION processInfo {};

  // CREATE_NEW_CONSOLE so the local server's log (heartbeat, snapshot, script output) is visible in its
  // own window during singleplayer rather than being swallowed.
  if (!CreateProcessW(applicationName.c_str(), commandBuffer.data(), nullptr, nullptr, FALSE,
                      CREATE_NEW_CONSOLE, nullptr, workingDir.c_str(), &startupInfo, &processInfo))
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

  // Build argv: the server path followed by the (space-separated ASCII) launch flags. execv wants a
  // null-terminated array of writable C strings, so keep the backing strings alive in argStorage.
  std::vector<std::string> argStorage { serverExePath };
  std::istringstream argStream(arguments);
  for (std::string token; argStream >> token;)
  {
    argStorage.push_back(token);
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
    // Child: match the headless server's working directory, then replace the image. execv only returns
    // on failure, so any path past it is an error.
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
