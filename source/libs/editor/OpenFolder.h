#ifndef OPENFOLDER_H
#define OPENFOLDER_H

#include <filesystem>

// Opens a folder in the platform's file browser without going through a shell. Returns false when the launch failed.
[[nodiscard]] bool openFolderInFileBrowser(const std::filesystem::path& folder);

#endif
