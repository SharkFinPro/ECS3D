#include "ServerProcess.h"
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
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
  // TODO: POSIX fork/exec (+ kill on destruction) for the local server.
  (void)exeBaseName;
  return false;
#endif
}

}
