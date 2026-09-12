#include "DarkTitleBar.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
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
  const bool applied = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark)) == S_OK
    || DwmSetWindowAttribute(hwnd, dwmwaUseImmersiveDarkModeLegacy, &useDark, sizeof(useDark)) == S_OK;

  if (applied)
  {
    // Windows 10 doesn't repaint an existing window's non-client area for this attribute until the
    // window is deactivated; force that repaint now instead of waiting for it.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
}

#else

void applyDarkTitleBar(GLFWwindow*, bool)
{
}

#endif
