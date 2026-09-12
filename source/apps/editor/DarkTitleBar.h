#ifndef DARKTITLEBAR_H
#define DARKTITLEBAR_H

struct GLFWwindow;

// Sets the native window's title bar to follow the editor's dark/light theme. Windows only, via
// DWMWA_USE_IMMERSIVE_DARK_MODE - a no-op elsewhere, since macOS title bars already follow the system
// appearance and most Linux window managers draw their own chrome.
void applyDarkTitleBar(GLFWwindow* window, bool dark);

#endif //DARKTITLEBAR_H
