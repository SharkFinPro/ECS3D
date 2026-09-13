#ifndef CONSOLEWINDOW_H
#define CONSOLEWINDOW_H

// Opens a console window for a GUI-subsystem process and points stdout/stderr (and std::cout/std::cerr)
// at it. Returns whether a console is available afterwards. A no-op returning true off Windows.
bool openConsoleWindow();

#endif  // CONSOLEWINDOW_H
