#include "DarkTitleBar.h"

#ifdef _WIN32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <dwmapi.h>

// Declared by SDKs new enough to know about immersive dark mode; define it ourselves so this still
// builds against an older Windows SDK.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {
  constexpr DWORD dwmwaUseImmersiveDarkModeLegacy = 19;  // Windows 10 builds before 20H1
}

void applyDarkTitleBar(GLFWwindow* window, const bool dark)
{
  const HWND hwnd = glfwGetWin32Window(window);
  if (!hwnd)
  {
    return;
  }

  const BOOL useDark = dark ? TRUE : FALSE;
  if (DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark)) != S_OK)
  {
    DwmSetWindowAttribute(hwnd, dwmwaUseImmersiveDarkModeLegacy, &useDark, sizeof(useDark));
  }
}

#else

void applyDarkTitleBar(GLFWwindow*, bool)
{
}

#endif
