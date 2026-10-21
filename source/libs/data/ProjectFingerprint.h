#ifndef PROJECTFINGERPRINT_H
#define PROJECTFINGERPRINT_H

#include <cstdint>
#include <filesystem>
#include <string_view>

// Names the project file a process loaded by its content, so an editor and a server on different machines
// can tell whether they hold the same project. Zero means no file: the built-in project, one an editor
// sent, or an unreadable file. A real file never hashes to zero.
[[nodiscard]] uint64_t fingerprintBytes(std::string_view bytes);

[[nodiscard]] uint64_t fingerprintProjectFile(const std::filesystem::path& projectFile);

#endif //PROJECTFINGERPRINT_H
