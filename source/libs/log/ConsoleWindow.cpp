#include "ConsoleWindow.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <iostream>

bool openConsoleWindow()
{
  if (GetConsoleWindow() != nullptr)
  {
    return true;
  }

  if (!AllocConsole())
  {
    return false;
  }

  FILE* f = nullptr;
  if (freopen_s(&f, "CONOUT$", "w", stdout) != 0 || freopen_s(&f, "CONOUT$", "w", stderr) != 0)
  {
    return false;
  }

  // std::cout/std::cerr may have set failbit while no console backed them; clear that now that
  // stdout/stderr point at one.
  std::cout.clear();
  std::cerr.clear();

  return true;
}

#else

bool openConsoleWindow()
{
  return true;
}

#endif
