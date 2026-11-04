#include "OpenFolder.h"
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

bool openFolderInFileBrowser(const std::filesystem::path& folder)
{
#if defined(_WIN32)
  const auto result = reinterpret_cast<INT_PTR>(
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));

  return result > 32;
#else
#if defined(__APPLE__)
  const char* program = "open";
  char** environment = *_NSGetEnviron();
#else
  const char* program = "xdg-open";
  char** environment = environ;
#endif

  const std::string path = folder.string();
  char* const arguments[] = {const_cast<char*>(program), const_cast<char*>(path.c_str()), nullptr};
  pid_t child = 0;

  if (posix_spawnp(&child, program, nullptr, nullptr, arguments, environment) != 0)
  {
    return false;
  }

  // Reaps the launcher so it does not stay a zombie.
  std::thread([child] {
    int status = 0;
    waitpid(child, &status, 0);
  }).detach();

  return true;
#endif
}
