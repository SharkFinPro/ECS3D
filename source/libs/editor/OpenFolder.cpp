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
#include <cerrno>
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

  // Linux has no spawn flag to close inherited descriptors, and the editor's own descriptors are not marked
  // close-on-exec in general, so only macOS can keep them out of the launcher.
  posix_spawnattr_t attributes;
  posix_spawn_file_actions_t actions;
  const bool hasAttributes = posix_spawnattr_init(&attributes) == 0;
  const bool hasActions = posix_spawn_file_actions_init(&actions) == 0;
  bool restrictDescriptors = false;
#if defined(__APPLE__)
  if (hasAttributes && hasActions)
  {
    restrictDescriptors = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT) == 0;

    for (int descriptor = 0; descriptor < 3 && restrictDescriptors; ++descriptor)
    {
      restrictDescriptors = posix_spawn_file_actions_addinherit_np(&actions, descriptor) == 0;
    }
  }
#endif

  const int spawned = posix_spawnp(&child, program, restrictDescriptors ? &actions : nullptr,
                                   restrictDescriptors ? &attributes : nullptr, arguments, environment);

  if (hasAttributes)
  {
    posix_spawnattr_destroy(&attributes);
  }

  if (hasActions)
  {
    posix_spawn_file_actions_destroy(&actions);
  }

  if (spawned != 0)
  {
    return false;
  }

  // Reaps the launcher so it does not stay a zombie.
  std::thread([child] {
    int status = 0;

    while (waitpid(child, &status, 0) == -1 && errno == EINTR)
    {
    }
  }).detach();

  return true;
#endif
}
